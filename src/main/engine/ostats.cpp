/***************************************************************************
    In-Game Statistics - CannonBall DX Endless wrapper.

    The preserved OStats implementation remains in ostats_base.cpp. Endless
    adds run-distance tracking, stage transition banners and clean music
    changes on top of the original timer/lap handling.
***************************************************************************/

#include <cstdio>
#include <cstring>

// Load all preserved dependencies before renaming do_timers so the temporary
// macro cannot touch declarations in another header.
#include "engine/ohud.hpp"
#include "engine/omusic.hpp"
#include "engine/outils.hpp"
#include "engine/ostats.hpp"
#include "engine/otraffic.hpp"
#include "engine/oinitengine.hpp"
#include "engine/endless_hiscore.hpp"

#define do_timers do_timers_base
#include "ostats_base.cpp"
#undef do_timers

extern EndlessHiScore endless_hiscore;

namespace
{
    int last_endless_stage = -1;
    int endless_banner_ticks = 0;
    bool endless_banner_difficulty = false;
    char endless_banner_text[40] = {0};

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

    void begin_endless_banner()
    {
        const unsigned stage_number =
            static_cast<unsigned>(outrun.endless_stage) + 1;

        std::snprintf(
            endless_banner_text,
            sizeof(endless_banner_text),
            "STAGE %u %s",
            stage_number,
            endless_stage_name(static_cast<uint8_t>(oroad.stage_lookup_off)));

        // Difficulty steps every three completed stages while either traffic
        // or checkpoint time is still becoming harder. The final time step is
        // reached at endless_stage 45 (30 second floor).
        endless_banner_difficulty =
            outrun.endless_stage > 0 &&
            (outrun.endless_stage % 3) == 0 &&
            outrun.endless_stage <= 45;

        // OStats::do_timers is driven at 60 Hz, so 120 ticks is about two
        // seconds regardless of 30/60 fps rendering mode.
        endless_banner_ticks = 120;
    }

    void draw_endless_banner()
    {
        if (endless_banner_ticks <= 0)
            return;

        ohud.blit_text_big(4, endless_banner_text);
        ohud.blit_text_big(
            7,
            endless_banner_difficulty ? "DIFFICULTY UP" : "");

        if (--endless_banner_ticks == 0)
        {
            // Clear only the rows owned by the temporary Endless overlay.
            ohud.blit_text_big(4, "");
            ohud.blit_text_big(7, "");
        }
    }

    void reset_endless_tracking()
    {
        last_endless_stage = -1;
        endless_banner_ticks = 0;
        endless_banner_difficulty = false;
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
            // Keep music changes at checkpoints so a song is never cut in the
            // middle of a stage. Four stages is close to one full arcade song
            // at normal Endless pace and prevents the selected track from
            // repeatedly looping for long runs.
            if (last_endless_stage >= 0 && stage > 0 && (stage % 4) == 0)
                omusic.cycle_music();

            last_endless_stage = stage;
            begin_endless_banner();
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
