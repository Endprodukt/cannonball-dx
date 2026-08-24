/***************************************************************************
    Best Outrunners - CannonBall DX score-screen wrapper.

    The original score implementation is preserved in ohiscore_base.cpp.
    Endless and Time Trial substitute their dedicated tables only at game end.

    DX attract mode uses one common Best OutRunners presentation for every
    score category: the original title, a mode label, column headers and five
    visible entries. The stock mini-car reveal is retained, but custom tables
    use stable field palettes rather than the original position-based
    Route/Name/Record palette assumptions.
***************************************************************************/

#include "main.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/oroad.hpp"
#include "engine/ostats.hpp"
#include "engine/outils.hpp"
#include "engine/ohiscore.hpp"
#include "engine/oinitengine.hpp"
#include "engine/opalette.hpp"
#include "engine/otiles.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <string>

#include "engine/endless_hiscore.hpp"
#include "engine/time_trial_records.hpp"

#define init init_base
#define tick tick_base
#define display_scores display_scores_base
#define score_position score_position_base
#include "ohiscore_base.cpp"
#undef score_position
#undef display_scores
#undef tick
#undef init

EndlessHiScore endless_hiscore;
TimeTrialRecords time_trial_records;

namespace
{
    enum AttractScorePage
    {
        ATTRACT_SCORE_ORIGINAL = 0,
        ATTRACT_SCORE_ORIGINAL_JAPAN,
        ATTRACT_SCORE_CONTINUOUS,
        ATTRACT_SCORE_ENDLESS,
        ATTRACT_SCORE_TTRIAL_WITH_1,
        ATTRACT_SCORE_TTRIAL_WITH_2,
        ATTRACT_SCORE_TTRIAL_WITH_3,
        ATTRACT_SCORE_TTRIAL_WITHOUT_1,
        ATTRACT_SCORE_TTRIAL_WITHOUT_2,
        ATTRACT_SCORE_TTRIAL_WITHOUT_3,
        ATTRACT_SCORE_PAGE_COUNT
    };

    const int ATTRACT_SCORE_SECONDS = 5;
    const int ATTRACT_VISIBLE_ROWS = 5;
    const uint16_t ATTRACT_ROW_BOTTOM_Y = 10;

    bool endless_score_audio_started = false;
    bool time_trial_score_audio_started = false;

    bool attract_score_rotation_active = false;
    int attract_score_page = ATTRACT_SCORE_ORIGINAL;
    int attract_saved_mode = Outrun::MODE_ORIGINAL;
    int attract_saved_jap = 0;

    bool endless_gameover_score_screen()
    {
        return outrun.endless_mode &&
               outrun.cannonball_mode == Outrun::MODE_CONT &&
               (outrun.game_state == GS_INIT_BEST2 ||
                outrun.game_state == GS_BEST2);
    }

    bool time_trial_record_screen()
    {
        return outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
               (outrun.game_state == GS_INIT_BEST2 ||
                outrun.game_state == GS_BEST2);
    }

    bool attract_score_screen()
    {
        return outrun.game_state == GS_INIT_BEST1 ||
               outrun.game_state == GS_BEST1;
    }

    bool attract_time_trial_page(int page)
    {
        return page >= ATTRACT_SCORE_TTRIAL_WITH_1 &&
               page <= ATTRACT_SCORE_TTRIAL_WITHOUT_3;
    }

    bool attract_time_trial_with_traffic(int page)
    {
        return page >= ATTRACT_SCORE_TTRIAL_WITH_1 &&
               page <= ATTRACT_SCORE_TTRIAL_WITH_3;
    }

    int attract_time_trial_page_index(int page)
    {
        if (attract_time_trial_with_traffic(page))
            return page - ATTRACT_SCORE_TTRIAL_WITH_1;

        return page - ATTRACT_SCORE_TTRIAL_WITHOUT_1;
    }

    void stabilize_score_background()
    {
        oroad.init();
        oroad.stage_lookup_off = 0;
        oinitengine.init_road_seg_master();

        otiles.init();
        otiles.reset_tiles_pal();
        otiles.setup_palette_hud();
        otiles.setup_palette_tilemap();
        otiles.update_tilemaps(0);
        otiles.write_tilemap_hw();

        opalette.setup_sky_palette();
        opalette.setup_ground_color();
        opalette.setup_road_centre();
        opalette.setup_road_stripes();
        opalette.setup_road_side();
        opalette.setup_road_colour();

        ohiscore.setup_pal_best();
        ohiscore.setup_road_best();

        oroad.set_view_mode(ORoad::VIEW_ORIGINAL, true);
        oroad.horizon_base = 0x154;
        oroad.horizon_set = 1;
        oroad.road_pos = 0;
        oroad.road_pos_change = 0;
        oroad.tilemap_h_target = 0;
    }

    uint8_t attract_tile_char(char value)
    {
        if (value == '\'')
            return 0x5E;
        if (value == '"')
            return 0x5F;
        if (value == '.')
            return 0x5B;
        return static_cast<uint8_t>(value);
    }

    void draw_attract_text(uint16_t x,
                           uint16_t y,
                           const std::string& text,
                           uint16_t colour)
    {
        uint32_t dst = ohud.translate(x, y);

        for (char value : text)
        {
            if (value == ' ')
                video.write_text16(&dst, 0);
            else
                video.write_text16(
                    &dst,
                    (colour << 8) | attract_tile_char(value));
        }
    }

    void draw_attract_centered(uint16_t y,
                               const std::string& text,
                               uint16_t colour)
    {
        int x = 20 - static_cast<int>(text.size() / 2);
        if (x < 0)
            x = 0;

        draw_attract_text(static_cast<uint16_t>(x), y, text, colour);
    }

    void draw_best_outrunners_title()
    {
        uint32_t src = TEXT2_BEST_OR;

        // The stock title normally sits lower. DX moves the two-line original
        // graphic up by two text rows so the mode and table have more air.
        uint32_t dst = 0x110000 + roms.rom0.read16(&src) - 0x100;

        uint16_t pal = roms.rom0.read8(&src);
        pal = 0x80A0 | ((pal << 9) | (pal >> 7) & 1);
        const uint16_t counter = roms.rom0.read8(&src);

        for (uint16_t i = 0; i <= counter; i++)
        {
            uint16_t data = roms.rom0.read8(&src);

            if (data == 0x20)
            {
                video.write_text16(&dst, 0);
                video.write_text16(0x7E + dst, 0);
                continue;
            }

            data -= 0x41;
            data = (data * 2) + pal;
            video.write_text16(&dst, data);
            video.write_text16(0x7E + dst, data + 1);
        }
    }

    void clear_score_tile_page()
    {
        uint32_t tile_addr = 0x10E000;
        for (int i = 0; i <= 0x3FF; i++)
