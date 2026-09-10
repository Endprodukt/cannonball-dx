#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

#include <SDL.h>

#include "main.hpp"
#include "frontend/config.hpp"
#include "engine/audio/osoundint.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/ohud.hpp"
#include "engine/omusic.hpp"
#include "directx/ffeedback.hpp"
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

    // The regular HUD font is already CannonBall's compact 8x8 tile font.
    // Row 9 keeps the temporary title close to the top edge while staying below
    // the normal top HUD and Time Trial's TIME TO BEAT / best-lap area.
    static constexpr uint16_t OVERLAY_Y = 9;
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

    inline bool& engine_vibration_active()
    {
        static bool value = false;
        return value;
    }

    inline int& engine_vibration_bucket()
    {
        static int value = -1;
        return value;
    }

    inline void stop_engine_vibration()
    {
        if (engine_vibration_active())
            forcefeedback::set_tyre_slip(false);

        engine_vibration_active() = false;
        engine_vibration_bucket() = -1;
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
        {
            const std::string device = config.radio_binding_device(BINDING_GAMEPAD);
            const int index = config.radio_binding_index(BINDING_GAMEPAD);

            if (device == "!")
                return false;

            if (index >= 0 && !device.empty())
                return physical_binding_pressed(BINDING_GAMEPAD);

            // Standard SDL layout: L3 / left-stick click is Radio.
            return input.joy_button == SDL_CONTROLLER_BUTTON_LEFTSTICK &&
                input.is_gamepad_device(input.joy_button_device);
        }

        return physical_binding_pressed(BINDING_WHEEL);
    }

    inline void stop_music()
    {
        // Stop both possible music backends. This is intentionally done before
        // every manual station change, not only for MUSIC OFF. A WAV/MP3 track
        // is mixed independently from the YM chip, so starting a file without
        // resetting YM leaves the previous arcade song playing underneath it.
        cannonball::audio.clear_wav();
        osoundint.queue_sound(sound::FM_RESET);
    }

    // Keep the existing grid rev-shake and the new driving vibration as one
    // logical ENGINE VIBRATION effect. The FFB backend identifies calls from a
    // function containing "update_prestart_sine", so the start-grid and race
    // paths share one periodic motor effect and one user-facing strength value.
    inline void update_prestart_sine_engine_vibration()
    {
        const bool can_run =
            cannonball::state == cannonball::STATE_GAME &&
            outrun.game_state == GS_INGAME &&
            config.input_mode_is_wheel() &&
            config.controls.haptic &&
            forcefeedback::is_supported() &&
            !ocrash.crash_counter &&
            !ocrash.skid_counter &&
            !outrun.SkiddingOnRoad() &&
            oferrari.wheel_state == OFerrari::WHEELS_ON;

        if (!can_run)
        {
            // Frontend/game-state transitions are not another driving effect:
            // stop the motor sine immediately (notably when F5 opens the menu).
            // Crash/skid/off-road are different: OOutputs may already have
            // handed the shared periodic channel to tyre slip earlier this frame,
            // so only reset our local ownership markers there.
            if (cannonball::state != cannonball::STATE_GAME ||
                outrun.game_state != GS_INGAME)
            {
                stop_engine_vibration();
            }
            else
            {
                engine_vibration_active() = false;
                engine_vibration_bucket() = -1;
            }
            return;
        }

        // The HUD rev bar uses (revs >> 16) >> 4 and spans roughly 0..0x13.
        // Treat 0x130 as the useful top of the rev range and quantize it into
        // eight steps. That prevents expensive haptic rebuilds every frame while
        // still making the motor clearly build with RPM.
        int revs = static_cast<int>(oferrari.revs >> 16);
        if (revs < 0)
            revs = 0;
        if (revs > 0x130)
            revs = 0x130;

        int rev_percent = (revs * 100 + 0x98) / 0x130;
        int bucket = (rev_percent * 7 + 50) / 100;
        if (bucket < 0)
            bucket = 0;
        else if (bucket > 7)
            bucket = 7;

        // Keep the effect subtle at idle/low revs and let it build smoothly.
        // This is an envelope only: ENGINE VIBRATION is the actual maximum
        // strength, while ENGINE PERIOD controls the low-RPM pulse spacing.
        const int envelope_percent = 18 + ((bucket * 82 + 3) / 7);

        if (!engine_vibration_active() || bucket != engine_vibration_bucket())
        {
            if (engine_vibration_active())
                forcefeedback::set_tyre_slip(false);

            forcefeedback::set_gain(envelope_percent);
            forcefeedback::set_tyre_slip(true);
            forcefeedback::set_gain(config.controls.ffb_strength);

            engine_vibration_active() = true;
            engine_vibration_bucket() = bucket;
        }
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

    inline void show_overlay(const std::string& message)
    {
        // Clear the complete previous label first. Without this, switching from
        // a long title to a shorter one leaves the tail of the old title behind.
        clear_overlay();

        std::string text = message;
        if (text.size() > 36)
            text.resize(36);

        overlay_text() = text;
        overlay_length() = static_cast<int>(text.size());
        overlay_x() = std::max(0, (40 - overlay_length()) / 2);
        overlay_deadline() = SDL_GetTicks() + OVERLAY_TIME_MS;
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

        // Always stop the previous backend before starting the next track.
        // This is essential when moving from YM music to a WAV/MP3 track,
        // because those are independent mixer sources and otherwise overlap.
        stop_music();

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
        // This per-frame hook already exists regardless of whether external
        // cabinet outputs are enabled, so it is also a convenient place to keep
        // the unified grid/race engine vibration alive during normal gameplay.
        update_prestart_sine_engine_vibration();

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
