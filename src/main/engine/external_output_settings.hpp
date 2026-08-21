#pragma once

#include <cstring>
#include <string>

#include "main.hpp"
#include "roms.hpp"
#include "video.hpp"
#include "engine/external_outputs.hpp"
#include "engine/ohud.hpp"
#include "engine/oroad.hpp"
#include "engine/ostats.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "frontend/xml_parser.h"
#include "sdl2/input.hpp"

// Optional external-output transport settings. SmartyPi remains independent.
struct ExternalOutputSettings
{
    bool loaded = false;

    bool network = true;
    bool windows = true;
    int port = 8000;

    void load_once()
    {
        if (loaded)
            return;

        loaded = true;

        xml_parser::ptree tree;
        bool found = xml_parser::read_xml(config.data.cfg_file, tree);

        if (!found)
        {
            const std::string fallback = config.data.res_path + config.data.cfg_file;
            found = xml_parser::read_xml(fallback, tree);
        }

        if (!found)
            return;

        network = tree.get_int("outputs.network", 1) != 0;
        windows = tree.get_int("outputs.windows", 1) != 0;
        port = tree.get_int("outputs.port", 8000);
    }
};

inline ExternalOutputSettings& external_output_settings()
{
    static ExternalOutputSettings settings;
    settings.load_once();
    return settings;
}

// Enhanced-attract showcase wrapper. The normal Enhanced Attract drive is
// reused directly: no demo level, speed override, traffic override, road reset
// or engine pause is used. Each camera is announced first, then applied while
// the live attract drive continues uninterrupted.
class ExternalOutputsWithAttractShowcase : public ExternalOutputs
{
public:
    bool is_showcase_active() const
    {
        return showcase_active;
    }

    void update(bool enable_network,
                bool enable_windows,
                int port,
                bool application_running,
                int start_lamp,
                int brake_lamp,
                int view_lamp,
                int view1_lamp,
                int view2_lamp,
                int view3_lamp)
    {
        update_showcase();

        if (showcase_active)
        {
            // The dedicated VR lamps always follow the camera that is actually
            // on screen, including during the two-second announcement period.
            view_lamp = 0;
            view1_lamp = 0;
            view2_lamp = 0;
            view3_lamp = 0;

            const bool fast_blink = (cannonball::frame & 0x08) != 0;
            const uint8_t view = oroad.get_view_mode();

            if (view == ORoad::VIEW_ORIGINAL)
            {
                if (manual_override)
                {
                    view1_lamp = fast_blink ? 1 : 0;
                }
                else
                {
                    // Automatic ORIGINAL uses the existing ping-pong lamp chase.
                    const uint8_t chase =
                        static_cast<uint8_t>(((cannonball::frame - phase_start_frame) >> 3) & 3);
                    view1_lamp = chase == 0 ? 1 : 0;
                    view2_lamp = (chase == 1 || chase == 3) ? 1 : 0;
                    view3_lamp = chase == 2 ? 1 : 0;
                }
            }
            else if (view == ORoad::VIEW_ELEVATED)
            {
                view2_lamp = fast_blink ? 1 : 0;
            }
            else if (view == ORoad::VIEW_INCAR)
            {
                view3_lamp = fast_blink ? 1 : 0;
            }
        }

        ExternalOutputs::update(
            enable_network,
            enable_windows,
            port,
            application_running,
            start_lamp,
            brake_lamp,
            view_lamp,
            view1_lamp,
            view2_lamp,
            view3_lamp);
    }

private:
    // Each view is announced for two seconds while the previous live camera
    // continues to drive, then demonstrated for six seconds after the switch.
    static constexpr uint32_t ANNOUNCE_TIME_TICKS = 60; // 2 seconds at 30 Hz.
    static constexpr uint32_t VIEW_TIME_TICKS = 180;    // 6 seconds at 30 Hz.

    // Enhanced Attract starts each driving section at BCD 0x80 (80 seconds).
    // Trigger the in-section presentation at the real halfway point: 40 seconds
    // remaining. Using the game timer means menu visits automatically pause it.
    static constexpr uint8_t ATTRACT_MIDPOINT_BCD = 0x40;

    // After a Logo screen we additionally introduce the views shortly after the
    // normal attract drive resumes. tick_counter follows the 30 Hz engine logic
    // and therefore does not advance while the game is in the menu.
    static constexpr uint32_t POST_LOGO_DELAY_TICKS = 135; // 4.5 seconds at 30 Hz.

    bool showcase_active = false;
    bool showcase_announcing = false;
    bool manual_override = false;

    // Every real GS_ATTRACT driving section gets one midpoint showcase. After a
    // Logo -> Attract transition it additionally gets one early showcase after
    // 4.5 seconds. These flags describe the current driving section only.
    bool midpoint_showcase_pending = false;
    bool post_logo_resume_pending = false;
    bool post_logo_showcase_pending = false;
    uint32_t post_logo_due_tick = 0;

    bool view1_old = false;
    bool view2_old = false;
    bool view3_old = false;
    bool viewpoint_old = false;
    int previous_game_state = -1;
    int showcase_phase = -1;
    uint8_t phase_view = ORoad::VIEW_ORIGINAL;
    uint8_t announcement_hold_view = ORoad::VIEW_ORIGINAL;
    uint32_t phase_start_frame = 0;
    uint32_t phase_due_tick = 0;
    Outrun::AttractRuntimeState attract_cycle_state{};
    bool attract_cycle_saved = false;

    void clear_double_row(uint8_t y)
    {
        for (int x = 0; x < 40; x++)
        {
            video.write_text16(ohud.translate(x, y), 0);
            video.write_text16(ohud.translate(x, y) + 0x80, 0);
        }
    }

    void draw_double_row_centered(uint8_t y, const char* text, uint16_t pal)
    {
        int length = static_cast<int>(std::strlen(text));
        if (length > 40)
            length = 40;

        const int x_start = 20 - (length / 2);
        clear_double_row(y);

        uint32_t dst_addr = ohud.translate(x_start, y);

        for (int i = 0; i < length; i++)
        {
            uint16_t c = static_cast<uint8_t>(text[i]);

            if (c >= 'a' && c <= 'z')
                c -= 0x20;

            if (c == ' ')
            {
                video.write_text16(&dst_addr, 0);
                video.write_text16(0x7E + dst_addr, 0);
            }
            else if (c >= 'A' && c <= 'Z')
            {
                c = ((c - 'A') * 2) + pal;
                video.write_text16(&dst_addr, c);
                video.write_text16(0x7E + dst_addr, c + 1);
            }
        }
    }

    const char* phase_view_name() const
    {
        if (phase_view == ORoad::VIEW_ELEVATED)
            return "ELEVATED VIEW";
        if (phase_view == ORoad::VIEW_INCAR)
            return "IN CAR VIEW";
        return "ORIGINAL VIEW";
    }

    void draw_showcase_text()
    {
        // Extract the exact palette used by the original SELECT MUSIC prompt.
        uint32_t select_music_style = TEXT2_SELECT_MUSIC + 2;
        uint16_t prompt_pal = roms.rom0.read8(&select_music_style);
        prompt_pal = 0x80A0 | ((prompt_pal << 9) | ((prompt_pal >> 7) & 1));

        draw_double_row_centered(2, "TRY THREE DIFFERENT VIEWS", prompt_pal);
        draw_double_row_centered(4, "WITH THE VR BUTTONS!", prompt_pal);

        // During the two-second lead-in the upcoming view name is held solid so
        // the player reads the explanation before the camera actually changes.
        // Once the view is active, the name returns to the arcade-style blink.
        if (showcase_announcing || (cannonball::frame & 0x10))
            draw_double_row_centered(7, phase_view_name(), 0x8AA0);
        else
            clear_double_row(7);
    }

    bool manual_view_pressed()
    {
        const bool view1 = input.is_pressed(Input::VIEW1);
        const bool view2 = input.is_pressed(Input::VIEW2);
        const bool view3 = input.is_pressed(Input::VIEW3);
        const bool viewpoint = input.is_pressed(Input::VIEWPOINT);

        const bool pressed =
            (view1 && !view1_old) ||
            (view2 && !view2_old) ||
            (view3 && !view3_old) ||
            (viewpoint && !viewpoint_old);

        view1_old = view1;
        view2_old = view2;
        view3_old = view3;
        viewpoint_old = viewpoint;
        return pressed;
    }

    bool tick_due(uint32_t due) const
    {
        return static_cast<int32_t>(outrun.tick_counter - due) >= 0;
    }

    void begin_view_phase(int phase)
    {
        static const uint8_t VIEWS[] =
        {
            ORoad::VIEW_ORIGINAL,
            ORoad::VIEW_ELEVATED,
            ORoad::VIEW_INCAR,
        };

        showcase_phase = phase;
        phase_view = VIEWS[phase];
        announcement_hold_view = oroad.get_view_mode();
        manual_override = false;
        showcase_announcing = true;
        phase_start_frame = cannonball::frame;
        phase_due_tick = outrun.tick_counter + ANNOUNCE_TIME_TICKS;
    }

    void apply_announced_view()
    {
        showcase_announcing = false;
        manual_override = false;

        // Camera change only. Vehicle speed, AI, traffic, road position and all
        // other Enhanced Attract logic continue exactly as they normally would.
        oroad.set_view_mode(phase_view, true);
        phase_start_frame = cannonball::frame;
        phase_due_tick = outrun.tick_counter + VIEW_TIME_TICKS;
    }

    void start_showcase()
    {
        showcase_active = true;
        attract_cycle_state = outrun.capture_attract_runtime_state();
        attract_cycle_saved = true;
        video.clear_text_ram();
        begin_view_phase(0);
    }

    void finish_showcase()
    {
        showcase_announcing = false;

        // Re-sync the normal automatic view timer to the view on screen so the
        // next automatic attract change starts a fresh, predictable cycle.
        if (attract_cycle_saved)
        {
            attract_cycle_state.view = oroad.get_view_mode();
            attract_cycle_state.counter = 0;
            outrun.restore_attract_runtime_state(attract_cycle_state);
        }

        attract_cycle_saved = false;
        showcase_active = false;
        showcase_phase = -1;
        manual_override = false;
        video.clear_text_ram();
    }

    void abort_showcase()
    {
        showcase_active = false;
        showcase_announcing = false;
        showcase_phase = -1;
        manual_override = false;
        attract_cycle_saved = false;
    }

    bool post_logo_delay_elapsed() const
    {
        return tick_due(post_logo_due_tick);
    }

    void update_showcase()
    {
        const bool enhanced_game =
            cannonball::state == cannonball::STATE_GAME &&
            config.engine.new_attract;

        // Logo timeout changes GS_LOGO -> GS_INIT for one frame. Remember this
        // edge so the resumed driving section also gets its early presentation.
        if (enhanced_game &&
            previous_game_state == GS_LOGO &&
            outrun.game_state == GS_INIT)
        {
            post_logo_resume_pending = true;
        }

        // A transition into GS_ATTRACT means a new real driving section has
        // begun. Arm one midpoint presentation every time, including the first
        // attract after boot/game start. Menu visits do not create this edge,
        // because the underlying game_state remains GS_ATTRACT while in menu.
        if (enhanced_game &&
            outrun.game_state == GS_ATTRACT &&
            previous_game_state != GS_ATTRACT)
        {
            midpoint_showcase_pending = true;

            if (post_logo_resume_pending)
            {
                post_logo_resume_pending = false;
                post_logo_showcase_pending = true;
                post_logo_due_tick = outrun.tick_counter + POST_LOGO_DELAY_TICKS;
            }
        }

        // If Enhanced Attract itself is turned off, discard presentation work.
        // Do not do this merely because the menu is open: that is exactly what
        // lets the real attract timer survive a menu round-trip.
        if (!config.engine.new_attract)
        {
            midpoint_showcase_pending = false;
            post_logo_resume_pending = false;
            post_logo_showcase_pending = false;
        }

        if (!showcase_active && enhanced_game && outrun.game_state == GS_ATTRACT)
        {
            // Shortly after every Logo, show the early presentation first.
            if (post_logo_showcase_pending && post_logo_delay_elapsed())
            {
                post_logo_showcase_pending = false;
                start_showcase();
            }
            // Then show exactly one more presentation at the genuine halfway
            // point of this 80-second driving section. Because time_counter is
            // the engine timer, menu time does not advance it.
            else if (midpoint_showcase_pending &&
                     ostats.time_counter <= ATTRACT_MIDPOINT_BCD)
            {
                midpoint_showcase_pending = false;
                start_showcase();
            }
        }

        if (showcase_active)
        {
            if (!enhanced_game || outrun.game_state != GS_ATTRACT)
            {
                abort_showcase();
            }
            else
            {
                if (manual_view_pressed())
                    manual_override = true;

                if (showcase_announcing)
                {
                    // Keep the live view stable against the normal automatic
                    // Enhanced Attract view timer while the next view is being
                    // announced. A real VR-button press is still respected.
                    if (!manual_override && oroad.get_view_mode() != announcement_hold_view)
                        oroad.set_view_mode(announcement_hold_view, true);

                    draw_showcase_text();

                    if (tick_due(phase_due_tick))
                        apply_announced_view();
                }
                else
                {
                    // During the six-second demonstration hold the announced
                    // automatic view, but never fight a real player VR override.
                    if (!manual_override && oroad.get_view_mode() != phase_view)
                        oroad.set_view_mode(phase_view, true);

                    draw_showcase_text();

                    if (tick_due(phase_due_tick))
                    {
                        if (showcase_phase < 2)
                            begin_view_phase(showcase_phase + 1);
                        else
                            finish_showcase();
                    }
                }
            }
        }
        else
        {
            // Keep edge trackers current outside the showcase so a held VR
            // button cannot become a false new press at the next presentation.
            manual_view_pressed();
        }

        previous_game_state = outrun.game_state;
    }
};

// ooutputs.cpp declares its transport object after this header is included.
// Use the showcase-aware subclass without touching the existing output wrapper.
#define ExternalOutputs ExternalOutputsWithAttractShowcase
