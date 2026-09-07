#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

#include <SDL.h>

#include "main.hpp"
#include "frontend/config.hpp"
#include "engine/audio/osoundint.hpp"
#include "engine/ohud.hpp"
#include "engine/omusic.hpp"
#include "sdl2/input.hpp"

// In-game radio button support.
//
// The radio control deliberately lives beside the existing input matrix rather
// than consuming one of CannonBall's legacy pad/key slots. The binding editor
// still presents it as a normal KEYBOARD / GAMEPAD / WHEEL row, while runtime
// input reuses Input's existing button/HAT capture state and persistent device
// signatures. This keeps all original controls and cabinet motor-limit slots
// untouched.
namespace radio_button
{
    static constexpr int BINDING_GAMEPAD = 0;
    static constexpr int BINDING_WHEEL = 1;
    static constexpr int TYPE_BUTTON = 0;
    static constexpr int TYPE_HAT = 2;
    static constexpr uint16_t OVERLAY_Y = 23;
    static constexpr Uint32 OVERLAY_TIME_MS = 2200;

    inline bool& pressed_old()
    {
        static bool value = false;
        return value;
    }

    inline int& radio_selection()
    {
        // -1 means synchronize from the song selected on Music Select when the
        // next race becomes active. track_count itself represents MUSIC OFF.
        static int value = -1;
        return value;
    }

    inline int& off_anchor_selection()
    {
        static int value = -1;
        return value;
    }

    inline std::string& overlay_text()
    {
        static std::string value;
        return value;
    }

    inline int& overlay_x()
    {
        static int value = 0;
        return value;
    }

    inline int& overlay_length()
    {
        static int value = 0;
        return value;
    }

    inline Uint32& overlay_deadline()
    {
        static Uint32 value = 0;
        return value;
    }

    inline bool gameplay_active()
    {
        return cannonball::state == cannonball::STATE_GAME &&
            outrun.game_state >= GS_START1 &&
            outrun.game_state <= GS_BONUS;
    }

    inline bool signature_matches(SDL_JoystickID device, const std::string& wanted)
    {
        if (device < 0 || wanted.empty())
            return false;

        return input.get_device_signature(device) == wanted;
    }

    inline bool physical_binding_pressed(int group)
    {
        const int type = config.radio_binding_type(group);
        const int index = config.radio_binding_index(group);
        const int value = config.radio_binding_value(group);
        const std::string device = config.radio_binding_device(group);

        if (index < 0 || device.empty())
            return false;

        if (type == TYPE_BUTTON)
        {
            return input.joy_button == index &&
                signature_matches(input.joy_button_device, device);
        }

        if (type == TYPE_HAT)
        {
            return input.joy_hat == index &&
                input.joy_hat_value != SDL_HAT_CENTERED &&
                value != SDL_HAT_CENTERED &&
                (input.joy_hat_value & value) != 0 &&
                signature_matches(input.joy_hat_device, device);
        }

        return false;
    }

    inline bool configured_control_pressed()
    {
        const int keyboard = config.radio_key();
        if (keyboard >= 0 && input.key_press == keyboard)
            return true;

        if (config.input_mode_is_gamepad())
            return physical_binding_pressed(BINDING_GAMEPAD);

        return physical_binding_pressed(BINDING_WHEEL);
    }

    inline void stop_music()
    {
        // Stop only music. PCM effects/engine audio continue normally.
        cannonball::audio.clear_wav();
        osoundint.queue_sound(sound::FM_RESET);
    }

    inline std::string display_title(const std::string& source)
    {
        std::string text;
        text.reserve(source.size());

        for (char ch : source)
        {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (c >= 'a' && c <= 'z')
                text.push_back(static_cast<char>(c - ('a' - 'A')));
            else if ((c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') ||
                     c == ' ' || c == '-' || c == '.' || c == '&')
                text.push_back(static_cast<char>(c));
            else
                text.push_back(' ');
        }

        // Collapse duplicate spaces produced by unsupported characters.
        std::string compact;
        compact.reserve(text.size());
        bool last_space = false;
        for (char c : text)
        {
            const bool space = c == ' ';
            if (!space || !last_space)
                compact.push_back(c);
            last_space = space;
        }

        while (!compact.empty() && compact.front() == ' ')
            compact.erase(compact.begin());
        while (!compact.empty() && compact.back() == ' ')
            compact.pop_back();

        if (compact.empty())
            compact = "TRACK";

        return compact;
    }

    inline void show_overlay(const std::string& message)
    {
        std::string text = message;
        if (text.size() > 36)
            text.resize(36);

        overlay_text() = text;
        overlay_length() = static_cast<int>(text.size());
        overlay_x() = std::max(0, (40 - overlay_length()) / 2);
        overlay_deadline() = SDL_GetTicks() + OVERLAY_TIME_MS;
    }

    inline void clear_overlay()
    {
        if (overlay_length() <= 0)
            return;

        const std::string blank(static_cast<std::size_t>(overlay_length()), ' ');
        ohud.blit_text_new(
            static_cast<uint16_t>(overlay_x()),
            OVERLAY_Y,
            blank.c_str(),
            OHud::GREEN);

        overlay_text().clear();
        overlay_length() = 0;
        overlay_x() = 0;
        overlay_deadline() = 0;
    }

    inline void draw_overlay(bool active)
    {
        if (!active || overlay_deadline() == 0)
        {
            if (!active)
                clear_overlay();
            return;
        }

        if (static_cast<Sint32>(SDL_GetTicks() - overlay_deadline()) >= 0)
        {
            clear_overlay();
            return;
        }

        ohud.blit_text_new(
            static_cast<uint16_t>(overlay_x()),
            OVERLAY_Y,
            overlay_text().c_str(),
            OHud::GREEN);
    }

    inline void synchronize_selection()
    {
        const int track_count = static_cast<int>(config.sound.music.size());
        if (track_count <= 0)
        {
            radio_selection() = 0;
            return;
        }

        const int selected = omusic.get_music_selected();

        if (radio_selection() < 0)
        {
            radio_selection() =
                selected >= 0 && selected < track_count ? selected : 0;
            return;
        }

        // If the game changes music automatically (for example Continuous or
        // Endless at a checkpoint), keep the manual radio cursor aligned with
        // what is actually playing. MUSIC OFF is special: automatic changes are
        // immediately silenced again so OFF remains a deliberate player choice.
        if (radio_selection() == track_count)
        {
            if (off_anchor_selection() != selected)
            {
                stop_music();
                off_anchor_selection() = selected;
            }
        }
        else if (selected >= 0 && selected < track_count &&
                 selected != radio_selection())
        {
            radio_selection() = selected;
        }
    }

    inline void cycle()
    {
        const int track_count = static_cast<int>(config.sound.music.size());

        if (track_count <= 0)
        {
            stop_music();
            radio_selection() = 0;
            off_anchor_selection() = -1;
            show_overlay("MUSIC OFF");
            return;
        }

        synchronize_selection();

        int current = radio_selection();
        if (current < 0 || current > track_count)
            current = 0;

        const int next = (current + 1) % (track_count + 1);

        if (next == track_count)
        {
            stop_music();
            radio_selection() = track_count;
            off_anchor_selection() = omusic.get_music_selected();
            show_overlay("MUSIC OFF");
            return;
        }

        // Keep OMusic's selected-song state in sync as well as starting the
        // track. This makes later Continuous/Endless automatic changes continue
        // from the song the player actually chose with the radio button.
        omusic.play_radio_music(next);
        radio_selection() = next;
        off_anchor_selection() = -1;

        const std::string title = display_title(config.sound.music[next].title);
        show_overlay(std::string("RADIO  ") + title);
    }

    // Called once per engine frame from ExternalOutputs::update(). The return
    // value is also the dedicated Radio_lamp output: steady on during playable
    // driving states and deliberately off in Attract and Music Select.
    inline int tick()
    {
        const bool active = gameplay_active();
        const bool pressed = configured_control_pressed();
        const bool edge = pressed && !pressed_old();
        pressed_old() = pressed;

        if (!active)
        {
            radio_selection() = -1;
            off_anchor_selection() = -1;
            clear_overlay();
            return 0;
        }

        synchronize_selection();

        if (edge)
            cycle();

        draw_overlay(true);
        return 1;
    }
}
