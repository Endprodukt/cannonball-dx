#pragma once

#include <cstring>
#include <string>

#include "main.hpp"
#include "roms.hpp"
#include "video.hpp"
#include "engine/oferrari.hpp"
#include "engine/ohud.hpp"
#include "engine/oinitengine.hpp"
#include "engine/oinputs.hpp"
#include "engine/oroad.hpp"
#include "engine/osprites.hpp"
#include "engine/otraffic.hpp"
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

// Enhanced-attract showcase wrapper. external_outputs.hpp is included before
// this header, so deriving here lets the existing OOutputs call site remain
// untouched while the showcase can override the final lamp values.
class ExternalOutputsWithAttractShowcase : public ExternalOutputs
{
public:
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

            const bool fast_blink = (outrun.tick_counter & BIT_3) != 0;
            const uint8_t view = oroad.get_view_mode();

            if (view == ORoad::VIEW_ORIGINAL)
            {
                if (manual_override)
                {
                    view1_lamp = fast_blink ? 1 : 0;
                }
                else
                {
                    // Automatic ORIGINAL uses the same fast ping-pong chase as
                    // the enhanced attract mode: 1 -> 2 -> 3 -> 2 -> ...
                    const uint8_t chase =
                        static_cast<uint8_t>(((outrun.tick_counter - phase_start_tick) >> 3) & 3);
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
    static constexpr uint32_t VIEW_TIME = 120; // 4 seconds at the 30 Hz game tick.
    static constexpr uint32_t TOTAL_TIME = VIEW_TIME * 3;

    bool showcase_pending = false;
    bool showcase_active = false;
    bool manual_override = false;
    bool view1_old = false;
    bool view2_old = false;
    bool view3_old = false;
    bool viewpoint_old = false;
    int previous_game_state = -1;
    int showcase_phase = -1;
    uint8_t phase_view = ORoad::VIEW_ORIGINAL;
    uint32_t showcase_start_tick = 0;
    uint32_t phase_start_tick = 0;

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

        // Same yellow/black double-row style as the enhanced Music Select song
        // title. The selected view deliberately blinks like an arcade prompt.
        if (outrun.tick_counter & BIT_4)
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

    void hide_non_palm_opening_scenery()
    {
        // Stage 1's palm trees use the normal level-object routine. Hide the
        // special start-line/water/grass/cloud routines so the short showcase
        // reads as road + palms rather than a normal attract-stage scene.
        // Limit this to the stage-1 level-object slots; Ferrari/passenger/smoke
        // slots live outside this range and remain untouched.
        for (int i = 0; i <= 0x44; i++)
        {
            oentry* sprite = &osprites.jump_table[i];
            if ((sprite->control & OSprites::ENABLE) && sprite->function_holder != 0)
                sprite->control &= ~OSprites::ENABLE;
        }
    }

    void reset_showcase_segment(uint8_t view)
    {
        // Reuse the first few seconds of Coconut Beach for every camera. This
        // keeps all three comparisons on the same straight road section and
        // avoids ever reaching the first bend during the 12-second showcase.
        oroad.init();
        oinitengine.init(0);

        oferrari.car_ctrl_active = true;
        oinitengine.car_x_pos = 0;
        oinitengine.car_x_old = 0;
        oroad.car_x_bak = 0;

        // Start at full normal OutRun speed instead of accelerating from rest.
        oinitengine.car_increment = 0xFA << 16;
        oferrari.car_inc_old = 0xFA;
        oinputs.acc_adjust = 0xFF;
        oinputs.brake_adjust = 0;
        oinputs.steering_adjust = 0;

        // Present a normal single road immediately, without the wide start-grid
        // road geometry, and suppress every traffic slot/spawn during the demo.
        oroad.road_width = 0xD4 << 16;
        oroad.road_width_bak = 0xD4;
        otraffic.disable_traffic();
        otraffic.set_custom_max_traffic(0);
        otraffic.ai_traffic = 0;

        // Remove any hard-coded start-line objects left in the reusable slots.
        for (int i = 0; i <= 0x44; i++)
            osprites.jump_table[i].control &= ~OSprites::ENABLE;

        oroad.set_view_mode(view, true);
        phase_view = view;
        manual_override = false;
        phase_start_tick = outrun.tick_counter;
    }

    void start_showcase()
    {
        showcase_pending = false;
        showcase_active = true;
        showcase_phase = 0;
        showcase_start_tick = outrun.tick_counter;
        video.clear_text_ram();
        reset_showcase_segment(ORoad::VIEW_ORIGINAL);
    }

    void end_showcase()
    {
        showcase_active = false;
        showcase_phase = -1;
        manual_override = false;
        video.clear_text_ram();
        oroad.set_view_mode(ORoad::VIEW_ORIGINAL, true);
        otraffic.set_max_traffic();

        // GS_INIT restarts the existing enhanced attract timer/view sequence on
        // the next engine tick. No normal attract behaviour is replaced.
        outrun.game_state = GS_INIT;
    }

    void update_showcase()
    {
        const bool enhanced_game =
            cannonball::state == cannonball::STATE_GAME &&
            config.engine.new_attract;

        // Logo timeout changes GS_LOGO -> GS_INIT for one frame. Remember that
        // edge, then start only when GS_INIT has entered GS_ATTRACT next tick.
        if (enhanced_game &&
            previous_game_state == GS_LOGO &&
            outrun.game_state == GS_INIT)
        {
            showcase_pending = true;
        }

        if (showcase_pending &&
            enhanced_game &&
            outrun.game_state == GS_ATTRACT)
        {
            start_showcase();
        }

        if (showcase_active)
        {
            if (!enhanced_game || outrun.game_state != GS_ATTRACT)
            {
                showcase_active = false;
                showcase_pending = false;
                showcase_phase = -1;
                manual_override = false;
                otraffic.set_max_traffic();
            }
            else
            {
                const uint32_t elapsed = outrun.tick_counter - showcase_start_tick;

                if (elapsed >= TOTAL_TIME)
                {
                    end_showcase();
                }
                else
                {
                    const int phase = static_cast<int>(elapsed / VIEW_TIME);
                    static const uint8_t VIEWS[] =
                    {
                        ORoad::VIEW_ORIGINAL,
                        ORoad::VIEW_ELEVATED,
                        ORoad::VIEW_INCAR,
                    };

                    if (phase != showcase_phase)
                    {
                        showcase_phase = phase;
                        reset_showcase_segment(VIEWS[phase]);
                    }
                    else
                    {
                        if (manual_view_pressed())
                            manual_override = true;

                        // The existing Enhanced Attract view timer still ticks
                        // underneath us. Hold the showcase's intended automatic
                        // view against that timer, but never fight a real player
                        // VR-button override inside the current 4-second phase.
                        if (!manual_override && oroad.get_view_mode() != phase_view)
                            oroad.set_view_mode(phase_view, true);
                    }

                    // Keep the demonstration pinned to full speed, normal road
                    // width and centreline regardless of AI/collision decisions.
                    oinitengine.car_increment = 0xFA << 16;
                    oferrari.car_inc_old = 0xFA;
                    oinitengine.car_x_pos = 0;
                    oinputs.acc_adjust = 0xFF;
                    oinputs.brake_adjust = 0;
                    oinputs.steering_adjust = 0;
                    oroad.road_width = 0xD4 << 16;
                    oroad.road_width_bak = 0xD4;
                    otraffic.disable_traffic();
                    otraffic.set_custom_max_traffic(0);
                    otraffic.ai_traffic = 0;
                    hide_non_palm_opening_scenery();
                    draw_showcase_text();
                }
            }
        }
        else
        {
            // Keep edge trackers current outside the showcase so holding a VR
            // button across the transition cannot look like a fresh press.
            manual_view_pressed();
        }

        previous_game_state = outrun.game_state;
    }
};

// ooutputs.cpp declares its transport object after this header is included.
// Use the showcase-aware subclass without touching the existing output wrapper.
#define ExternalOutputs ExternalOutputsWithAttractShowcase
