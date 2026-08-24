/***************************************************************************
    Best Outrunners - CannonBall DX score-screen wrapper.

    The original score implementation is preserved in ohiscore_base.cpp.
    Endless and Time Trial substitute their dedicated tables only at game end.

    DX attract mode uses one common Best OutRunners presentation for every
    score category: the original title, a mode label, column headers and five
    visible entries. Time Trial keeps those five rows as a clipped viewport
    and scrolls all 15 courses smoothly through it.
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
        ATTRACT_SCORE_TTRIAL_WITH,
        ATTRACT_SCORE_TTRIAL_WITHOUT,
        ATTRACT_SCORE_PAGE_COUNT
    };

    const int ATTRACT_SCORE_SECONDS = 5;
    const int ATTRACT_VISIBLE_ROWS = 5;
    const int ATTRACT_TTRIAL_GUARD_SECONDS = 15;

    // Logical System 16 pixels. A Time Trial row is two 8px text rows high.
    // Ten row advances take us from courses 1-5 to courses 11-15.
    const int ATTRACT_TTRIAL_ROW_PIXELS = 16;
    const int ATTRACT_TTRIAL_SCROLL_PIXELS =
        (TimeTrialRecords::TRACK_COUNT - ATTRACT_VISIBLE_ROWS) *
        ATTRACT_TTRIAL_ROW_PIXELS;
    const int ATTRACT_TTRIAL_SCROLL_PIXELS_PER_SECOND = 64;

    const uint16_t ATTRACT_ROW_BOTTOM_Y = 10;
    const uint16_t ATTRACT_TTRIAL_VIEW_TOP_Y = 9;
    const uint16_t ATTRACT_TTRIAL_VIEW_BOTTOM_Y = 18;
    const int ATTRACT_TTRIAL_VIEW_TOP_PIXEL =
        ATTRACT_TTRIAL_VIEW_TOP_Y * 8;
    const int ATTRACT_TTRIAL_VIEW_BOTTOM_PIXEL =
        (ATTRACT_TTRIAL_VIEW_BOTTOM_Y + 1) * 8;
    const int ATTRACT_TTRIAL_FIRST_ROW_PIXEL =
        ATTRACT_ROW_BOTTOM_Y * 8;

    bool endless_score_audio_started = false;
    bool time_trial_score_audio_started = false;

    bool attract_score_rotation_active = false;
    int attract_score_page = ATTRACT_SCORE_ORIGINAL;
    int attract_saved_mode = Outrun::MODE_ORIGINAL;
    int attract_saved_jap = 0;

    std::array<std::string, TimeTrialRecords::TRACK_COUNT> attract_ttrial_rows{};
    int attract_ttrial_scroll_fp = 0;
    int attract_ttrial_scroll_offset = 0;
    int attract_ttrial_hold_frames = 0;
    bool attract_ttrial_scroll_started = false;
    bool attract_ttrial_overlay_ready = false;

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
        return page == ATTRACT_SCORE_TTRIAL_WITH ||
               page == ATTRACT_SCORE_TTRIAL_WITHOUT;
    }

    bool attract_time_trial_with_traffic(int page)
    {
        return page == ATTRACT_SCORE_TTRIAL_WITH;
    }

    void reset_time_trial_scroll_state()
    {
        attract_ttrial_scroll_fp = 0;
        attract_ttrial_scroll_offset = 0;
        attract_ttrial_hold_frames = 0;
        attract_ttrial_scroll_started = false;
        attract_ttrial_overlay_ready = false;
        video.tile_layer->clear_text_scroll_overlay();
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

        // Keep the original two-line title graphic, just higher on the screen.
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
