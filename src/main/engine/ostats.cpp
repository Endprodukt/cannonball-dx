/***************************************************************************
    In-Game Statistics - CannonBall DX Endless wrapper.

    The preserved OStats implementation remains in ostats_base.cpp. Endless
    adds run-distance tracking, delayed difficulty progression, stage banners
    and clean music changes on top of the original timer/lap handling.
***************************************************************************/

#include <cstdio>
#include <cstring>

// Load all preserved dependencies before renaming methods so the temporary
// macros cannot touch declarations in another header.
#include "engine/ohud.hpp"
#include "engine/omusic.hpp"
#include "engine/outils.hpp"
#include "engine/ostats.hpp"
#include "engine/otraffic.hpp"
#include "engine/oinitengine.hpp"
#include "engine/endless_hiscore.hpp"

#define do_timers do_timers_base
#define init_next_level init_next_level_base
#include "ostats_base.cpp"
#undef init_next_level
#undef do_timers

extern EndlessHiScore endless_hiscore;

namespace
{
    const uint8_t ENDLESS_MAX_DIFFICULTY = 7;

    enum DifficultyBanner
    {
        DIFF_BANNER_NONE = 0,
        DIFF_BANNER_UP,
        DIFF_BANNER_MAX,
    };

    int last_endless_stage = -1;
    int last_endless_difficulty = -1;
    int endless_banner_ticks = 0;
    DifficultyBanner endless_difficulty_banner = DIFF_BANNER_NONE;
    char endless_banner_text[40] = {0};

    uint8_t endless_difficulty_rank(uint16_t stage)
    {
        // Stages 1-5 stay at the introductory setting. The first increase is
        // applied when entering Stage 6, then every three stages thereafter.
        if (stage < 5)
            return 0;

        uint16_t rank = 1 + ((stage - 5) / 3);
        if (rank > ENDLESS_MAX_DIFFICULTY)
            rank = ENDLESS_MAX_DIFFICULTY;

        return static_cast<uint8_t>(rank);
    }

    uint8_t endless_checkpoint_seconds(uint8_t rank)
    {
        // Final rank is intentionally capped. Once MAX DIFFICULTY is reached
        // neither traffic nor checkpoint time gets any harsher.
        static const uint8_t SECONDS[] =
        {
            60, 56, 52, 48, 44, 40, 36, 30
        };

        if (rank > ENDLESS_MAX_DIFFICULTY)
            rank = ENDLESS_MAX_DIFFICULTY;

        return SECONDS[rank];
    }

    uint8_t endless_traffic(uint8_t rank)
    {
        uint16_t traffic = 2 + rank;
        if (traffic > 8)
            traffic = 8;
        return static_cast<uint8_t>(traffic);
    }

    uint8_t bcd_seconds(int seconds)
    {
        if (seconds < 0)
            seconds = 0;
        else if (seconds > 99)
            seconds = 99;

        return static_cast<uint8_t>(
            ((seconds / 10) << 4) | (seconds % 10));
    }

    int decimal_seconds(uint8_t bcd)
    {
        return ((bcd >> 4) * 10) + (bcd & 0x0F);
    }

    const char* endless_stage_name(uint8_t level)
    {
        switch (level)
        {
            case 0x00: return "COCONUT BEACH";
            case 0x09: return config.engine.jap ? "WHEAT FIELD" : "GATEWAY";
            case 0x08: return config.engine.jap ? "CLOUDY MOUNTAIN" : "DEVILS CANYON";
            case 0x12: return "DESERT";
            case 0x11: return "ALPS";
            case 0x10: return config.engine.jap ? "DEVILS CANYON" : "CLOUDY MOUNTAIN";
            case 0x1B: return "WILDERNESS";
            case 0x1A: return "OLD CAPITAL";
            case 0x19: return config.engine.jap ? "GATEWAY" : "WHEAT FIELD";
            case 0x18: return "SEASIDE TOWN";
            case 0x24: return "VINEYARD";
            case 0x23: return "DEATH VALLEY";
            case 0x22: return "DESOLATION HILL";
            case 0x21: return "AUTOBAHN";
            case 0x20: return "LAKESIDE";
            default:   return "UNKNOWN";
        }
    }

    void begin_endless_banner(uint8_t difficulty)
    {
        const unsigned stage_number =
            static_cast<unsigned>(outrun.endless_stage) + 1;

        std::snprintf(
            endless_banner_text,
            sizeof(endless_banner_text),
            "STAGE %u  %s",
            stage_number,
            endless_stage_name(static_cast<uint8_t>(oroad.stage_lookup_off)));

        endless_difficulty_banner = DIFF_BANNER_NONE;

        if (last_endless_difficulty >= 0 &&
            difficulty > last_endless_difficulty)
        {
            endless_difficulty_banner =
                difficulty == ENDLESS_MAX_DIFFICULTY ?
                    DIFF_BANNER_MAX : DIFF_BANNER_UP;
        }

        last_endless_difficulty = difficulty;

        // OStats::do_timers is driven at 60 Hz, so 120 ticks is about two
        // seconds regardless of 30/60 fps rendering mode.
        endless_banner_ticks = 120;
    }

    void draw_centered_small(uint8_t y, const char* text, uint16_t colour)
    {
        const int length = static_cast<int>(std::strlen(text));
        int x = 20 - (length / 2);
        if (x < 0)
            x = 0;

        ohud.blit_text_new(0, y, "                                        ", colour);
        ohud.blit_text_new(static_cast<uint16_t>(x), y, text, colour);
    }

    void draw_endless_banner()
    {
        if (endless_banner_ticks <= 0)
            return;

        // Keep the stage identification compact at the top. Difficulty uses
        // the larger two-row font lower on screen so the change is obvious.
        draw_centered_small(4, endless_banner_text, OHud::GREEN);

        if (endless_difficulty_banner == DIFF_BANNER_MAX)
            ohud.blit_text_big(10, "MAX DIFFICULTY");
        else if (endless_difficulty_banner == DIFF_BANNER_UP)
            ohud.blit_text_big(10, "DIFFICULTY UP");
        else
            ohud.blit_text_big(10, "");

        if (--endless_banner_ticks == 0)
        {
            draw_centered_small(4, "", OHud::GREEN);
            ohud.blit_text_big(10, "");
            endless_difficulty_banner = DIFF_BANNER_NONE;
        }
    }

    void reset_endless_tracking()
    {
        last_endless_stage = -1;
        last_endless_difficulty = -1;
        endless_banner_ticks = 0;
        endless_difficulty_banner = DIFF_BANNER_NONE;
        endless_banner_text[0] = 0;
    }
}

void OStats::do_timers()
{
    const bool endless_ingame =
        outrun.endless_mode &&
        outrun.cannonball_mode == Outrun::MODE_CONT &&
        outrun.game_state == GS_INGAME;

    if (endless_ingame)
    {
        endless_hiscore.tick_run(
            static_cast<uint16_t>(oinitengine.car_increment >> 16));

        const int stage = static_cast<int>(outrun.endless_stage);
        if (stage != last_endless_stage)
        {
            const uint8_t difficulty =
                endless_difficulty_rank(outrun.endless_stage);

            // The legacy Endless core still derives traffic directly from the
            // stage number. Override both the public setting and live spawn cap
            // here so Stages 1-5 remain genuinely Easy before progression starts.
            const uint8_t traffic = endless_traffic(difficulty);
            outrun.custom_traffic = traffic;
            otraffic.set_custom_max_traffic(traffic);

            // Keep music changes at checkpoints so a song is never cut in the
            // middle of a stage. Four stages is close to one full arcade song
            // at normal Endless pace and prevents the selected track from
            // repeatedly looping for long runs.
            if (last_endless_stage >= 0 && stage > 0 && (stage % 4) == 0)
                omusic.cycle_music();

            last_endless_stage = stage;
            begin_endless_banner(difficulty);
        }
    }
    else if (last_endless_stage != -1 || endless_banner_ticks != 0)
    {
        reset_endless_tracking();
    }

    do_timers_base();

    if (endless_ingame)
        draw_endless_banner();
}

void OStats::init_next_level()
{
    const bool endless_checkpoint =
        outrun.endless_mode &&
        outrun.cannonball_mode == Outrun::MODE_CONT &&
        outrun.game_state == GS_INGAME &&
        oinitengine.checkpoint_marker &&
        !outrun.freeze_timer;

    const uint8_t time_before = time_counter;

    init_next_level_base();

    if (endless_checkpoint)
    {
        const uint8_t difficulty =
            endless_difficulty_rank(outrun.endless_stage);
        const int total_seconds =
            decimal_seconds(time_before) +
            endless_checkpoint_seconds(difficulty);

        time_counter = bcd_seconds(total_seconds);
    }
}
