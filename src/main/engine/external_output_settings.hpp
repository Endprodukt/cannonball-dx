#pragma once

#include <chrono>
#include <cstring>
#include <string>

#include "main.hpp"
#include "roms.hpp"
#include "video.hpp"
#include "engine/external_outputs.hpp"
#include "engine/ohud.hpp"
#include "engine/oroad.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "frontend/xml_parser.h"
#include "sdl2/input.hpp"

// Main engine pause flag. The showcase owns this only for its short camera
// presentation freezes; audio and rendering continue normally.
extern bool pause_engine;

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
// reused directly: no demo level, speed override, traffic override or road
// reset is used. The showcase only changes the camera and briefly pauses the
// engine while introducing each view.
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
            // The dedicated VR lamps own the presentation during the showcase.
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
    using Clock = std::chrono::steady_clock;

    static constexpr std::chrono::milliseconds FREEZE_TIME{900};
    static constexpr std::chrono::seconds DRIVE_TIME{7};
    static constexpr std::chrono::seconds INITIAL_SHOWCASE_DELAY{30};
    static constexpr std::chrono::milliseconds POST_LOGO_SHOWCASE_DELAY{4500};

    bool showcase_active = false;
    bool showcase_freezing = false;
    bool manual_override = false;

    // Scheduling: the first presentation happens about 30 seconds after the
    // initial Enhanced Attract drive begins. Later presentations are armed by
    // Logo -> Attract and begin 4.5 seconds after the resumed drive starts.
    bool initial_attract_seen = false;
    bool first_showcase_started = false;
    bool delayed_showcase_pending = false;
    bool post_logo_resume_pending = false;
    Clock::time_point showcase_due{};

    bool view1_old = false;
    bool view2_old = false;
    bool view3_old = false;
    bool viewpoint_old = false;
    int previous_game_state = -1;
    int showcase_phase = -1;
    uint8_t phase_view = ORoad::VIEW_ORIGINAL;
    uint32_t phase_start_frame = 0;
    Clock::time_point phase_time{};
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

    void draw_showcase_text()
    {
        // Extract the exact palette used by the original SELECT MUSIC prompt.
        uint32_t select_music_style = TEXT2_SELECT_MUSIC + 2;
        uint16_t prompt_pal = roms.rom0.read8(&select_music_style);
        prompt_pal = 0x80A0 | ((prompt_pal << 9) | ((prompt_pal >> 7) & 1));

        draw_double_row_centered(2, "TRY THREE DIFFERENT VIEWS", prompt_pal);
        draw_double_row_centered(4, "WITH THE VR BUTTONS!", prompt_pal);

        const char* view_name = "ORIGINAL VIEW";
        const uint8_t view = oroad.get_view_mode();
        if (view == ORoad::VIEW_ELEVATED)
            view_name = "ELEVATED VIEW";
        else if (view == ORoad::VIEW_INCAR)
            view_name = "IN CAR VIEW";

        // Keep the name solid during the short freeze so the changed camera is
        // clearly explained. Once driving resumes it returns to the arcade blink.
        if (showcase_freezing || (cannonball::frame & 0x10))
            draw_double_row_centered(7, view_name, 0x8AA0);
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
        manual_override = false;
        showcase_freezing = true;
        phase_time = Clock::now();
        phase_start_frame = cannonball::frame;

        // The car keeps the exact speed/input state produced by Enhanced Attract.
        // We only switch the camera. A road pass re-renders the same road position
        // with the new snapped horizon before the short presentation freeze.
        oroad.set_view_mode(phase_view, true);
        oroad.tick();
        pause_engine = true;
    }

    void start_showcase()
    {
        delayed_showcase_pending = false;
        first_showcase_started = true;
        showcase_active = true;
        attract_cycle_state = outrun.capture_attract_runtime_state();
        attract_cycle_saved = true;
        video.clear_text_ram();
        begin_view_phase(0);
    }

    void finish_showcase()
    {
        pause_engine = false;
        showcase_freezing = false;

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
        pause_engine = false;
        showcase_active = false;
        showcase_freezing = false;
        showcase_phase = -1;
        manual_override = false;
        attract_cycle_saved = false;
    }

    void reset_schedule()
    {
        initial_attract_seen = false;
        first_showcase_started = false;
        delayed_showcase_pending = false;
        post_logo_resume_pending = false;
        showcase_due = Clock::time_point{};
    }

    void update_showcase()
    {
        const Clock::time_point now = Clock::now();
        const bool enhanced_game =
            cannonball::state == cannonball::STATE_GAME &&
            config.engine.new_attract;

        // If Enhanced Attract is disabled, the next activation gets a fresh
        // first-run schedule instead of inheriting an old pending timer.
        if (!config.engine.new_attract)
            reset_schedule();

        // First run: start counting when the initial real attract drive begins.
        // This deliberately does not depend on the High Score / Logo cycle.
        if (enhanced_game &&
            outrun.game_state == GS_ATTRACT &&
            !initial_attract_seen)
        {
            initial_attract_seen = true;
            delayed_showcase_pending = true;
            showcase_due = now + INITIAL_SHOWCASE_DELAY;
        }

        // Subsequent runs: Logo times out to GS_INIT for one frame. Arm the
        // delayed presentation here, but start its 4.5-second timer only once
        // the normal Enhanced Attract drive has actually resumed.
        if (enhanced_game &&
            first_showcase_started &&
            previous_game_state == GS_LOGO &&
            outrun.game_state == GS_INIT)
        {
            post_logo_resume_pending = true;
        }

        if (enhanced_game &&
            post_logo_resume_pending &&
            outrun.game_state == GS_ATTRACT)
        {
            post_logo_resume_pending = false;
            delayed_showcase_pending = true;
            showcase_due = now + POST_LOGO_SHOWCASE_DELAY;
        }

        if (!showcase_active &&
            delayed_showcase_pending &&
            enhanced_game &&
            outrun.game_state == GS_ATTRACT &&
            now >= showcase_due)
        {
            start_showcase();
        }

        if (showcase_active)
        {
            if (!enhanced_game || outrun.game_state != GS_ATTRACT)
            {
                abort_showcase();
            }
            else if (showcase_freezing)
            {
                // Ignore manual VR changes during the explanatory freeze;
                // the announced view owns this short presentation moment.
                manual_view_pressed();
                if (oroad.get_view_mode() != phase_view)
                {
                    oroad.set_view_mode(phase_view, true);
                    oroad.tick();
                }

                draw_showcase_text();

                if (now - phase_time >= FREEZE_TIME)
                {
                    showcase_freezing = false;
                    pause_engine = false;
                    phase_time = now;
                }
            }
            else
            {
                if (manual_view_pressed())
                    manual_override = true;

                // The existing Enhanced Attract timer may try to change the
                // view underneath the presentation. Hold the announced view
                // unless the player deliberately used a VR button.
                if (!manual_override && oroad.get_view_mode() != phase_view)
                    oroad.set_view_mode(phase_view, true);

                draw_showcase_text();

                if (now - phase_time >= DRIVE_TIME)
                {
                    if (showcase_phase < 2)
                        begin_view_phase(showcase_phase + 1);
                    else
                        finish_showcase();
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
