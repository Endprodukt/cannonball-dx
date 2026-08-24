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
#include "engine/audio/osoundint.hpp"
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

    // Normal completed laps keep the existing five flashes. The final lap also
    // carries the total time, so give that finish presentation eight flashes.
    // Each visible/hidden half-phase lasts roughly a quarter second at 60 Hz.
    const int TTRIAL_LAP_HALF_PHASE_TICKS = 15;
    const int TTRIAL_LAP_FLASHES = 5;
    const int TTRIAL_FINAL_LAP_FLASHES = 8;

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

    int last_ttrial_lap = -1;
    int ttrial_lap_banner_ticks = 0;
    int ttrial_lap_banner_total_ticks = 0;
    int ttrial_lap_banner_phase = -1;
    uint8_t ttrial_lap_time[3] = {0, 0, 0};
    uint8_t ttrial_total_time[3] = {0, 0, 0};
    bool ttrial_show_total = false;

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

    void clear_endless_laptime()
    {
        // The stock checkpoint routine briefly draws the previous LAP TIME.
        // Endless has its own stage-change presentation, so keep that legacy
        // overlay out of the way. The stock clear records also remove the
        // timer digits, avoiding the small orphaned LAP tile seen in testing.
        ohud.blit_text1(TEXT1_LAPTIME_CLEAR1);
        ohud.blit_text1(TEXT1_LAPTIME_CLEAR2);
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

    void clear_text_row(uint16_t y)
    {
        for (uint16_t x = 0; x < 40; x++)
            video.write_text16(ohud.translate(x, y), 0);
    }

    void clear_ttrial_no_traffic_score()
    {
        if (outrun.cannonball_mode != Outrun::MODE_TTRIAL ||
            outrun.ttrial.traffic ||
            outrun.game_state < GS_START1 ||
            outrun.game_state > GS_BONUS)
        {
            return;
        }

        // Time Trial itself does not use score for ranking. With Traffic OFF it
        // has no useful overtaking context either, so remove both the SCORE logo
        // and the zero score digits from the in-game HUD.
        ohud.blit_text_new(2, 1, "        ", OHud::GREY);
        ohud.blit_text_new(2, 2, "        ", OHud::GREY);

        uint32_t score_addr = 0x110150;
        for (int i = 0; i < 8; i++)
            video.write_text16(&score_addr, 0);
    }

    void clear_ttrial_lap_banner()
    {
        // Clear both the original TIME graphic rows and the large digit rows.
        // blit_text_big owns a separate two-row font path, so clear those labels
        // through the same helper that created them.
        clear_text_row(8);
        clear_text_row(9);
        clear_text_row(11);
        clear_text_row(12);
        ohud.blit_text_big(15, "");
        clear_text_row(18);
        clear_text_row(19);
    }

    void draw_ttrial_large_time(uint8_t y, const uint8_t* time)
    {
        if (!time)
            return;

        uint32_t addr = ohud.translate(16, y);
        const uint16_t APOSTROPHE = 0x835E;
        const uint16_t QUOTE = 0x835F;

        ohud.blit_large_digit(
            &addr,
            static_cast<uint8_t>((time[0] & 0x0F) << 1));
        video.write_text16(&addr, APOSTROPHE);
        ohud.blit_large_digit(
            &addr,
            static_cast<uint8_t>(((time[1] >> 4) & 0x0F) << 1));
        ohud.blit_large_digit(
            &addr,
            static_cast<uint8_t>((time[1] & 0x0F) << 1));
        video.write_text16(&addr, QUOTE);
        ohud.blit_large_digit(
            &addr,
            static_cast<uint8_t>(((time[2] >> 4) & 0x0F) << 1));
        ohud.blit_large_digit(
            &addr,
            static_cast<uint8_t>((time[2] & 0x0F) << 1));
    }

    void draw_ttrial_lap_banner()
    {
        clear_ttrial_lap_banner();

        // Use the original arcade HUD TIME graphic instead of synthesizing a
        // new LAP TIME heading. Reposition its two source rows to the centre.
        ohud.blit_text1(18, 8, HUD_TIME1);
        ohud.blit_text1(18, 9, HUD_TIME2);
        draw_ttrial_large_time(11, ttrial_lap_time);

        // On the final lap keep the just-completed lap visible and add the full
        // run time underneath. In the normal three-lap Time Trial this means
        // lap 3 and the three-lap total appear together as the GOAL sequence starts.
        if (ttrial_show_total)
        {
            ohud.blit_text_big(15, "TOTAL TIME");
            draw_ttrial_large_time(18, ttrial_total_time);
        }
    }

    void begin_ttrial_lap_banner(int completed_lap, const uint8_t* lap_ms)
    {
        if (completed_lap < 0 || completed_lap >= 5 || !lap_ms)
            return;

        const uint8_t* lap = outrun.ttrial.laptimes[completed_lap];
        ttrial_lap_time[0] = lap[0];
        ttrial_lap_time[1] = lap[1];
        ttrial_lap_time[2] = lap_ms[lap[2]];

        ttrial_show_total =
            completed_lap + 1 >= static_cast<int>(outrun.ttrial.laps);

        if (ttrial_show_total)
        {
            uint32_t total_counter = 0;
            for (int i = 0; i <= completed_lap; i++)
            {
                if (ostats.stage_counters[i] > 0)
                    total_counter +=
                        static_cast<uint16_t>(ostats.stage_counters[i]);
            }

            if (total_counter > 0xFFFF)
                total_counter = 0xFFFF;

            outils::convert_counter_to_time(
                static_cast<uint16_t>(total_counter),
                ttrial_total_time);
        }
        else
        {
            ttrial_total_time[0] = 0;
            ttrial_total_time[1] = 0;
            ttrial_total_time[2] = 0;
        }

        const int flashes =
            ttrial_show_total ? TTRIAL_FINAL_LAP_FLASHES : TTRIAL_LAP_FLASHES;
        ttrial_lap_banner_total_ticks =
            TTRIAL_LAP_HALF_PHASE_TICKS * flashes * 2;
        ttrial_lap_banner_ticks = ttrial_lap_banner_total_ticks;
        ttrial_lap_banner_phase = -1;

        // check_stage() can still create the old small BEST LAP overlay on a
        // record lap. The new centred banner replaces it for all completed laps.
        ostats.extend_play_timer = 0;
        ohud.blit_text1(TEXT1_LAPTIME_CLEAR1);
        ohud.blit_text1(TEXT1_LAPTIME_CLEAR2);
    }

    void tick_ttrial_lap_banner()
    {
        if (ttrial_lap_banner_ticks <= 0)
            return;

        const int elapsed =
            ttrial_lap_banner_total_ticks - ttrial_lap_banner_ticks;
        const int phase = elapsed / TTRIAL_LAP_HALF_PHASE_TICKS;
        const bool visible = (phase & 1) == 0;

        if (phase != ttrial_lap_banner_phase)
        {
            ttrial_lap_banner_phase = phase;
            if (visible)
                osoundint.queue_sound(sound::BEEP1);
        }

        if (visible)
            draw_ttrial_lap_banner();
        else
            clear_ttrial_lap_banner();

        if (--ttrial_lap_banner_ticks == 0)
        {
            clear_ttrial_lap_banner();
            ttrial_lap_banner_total_ticks = 0;
            ttrial_lap_banner_phase = -1;
        }
    }

    void reset_ttrial_lap_tracking()
    {
        if (ttrial_lap_banner_ticks > 0)
            clear_ttrial_lap_banner();

        last_ttrial_lap = -1;
        ttrial_lap_banner_ticks = 0;
        ttrial_lap_banner_total_ticks = 0;
        ttrial_lap_banner_phase = -1;
        ttrial_lap_time[0] = 0;
        ttrial_lap_time[1] = 0;
        ttrial_lap_time[2] = 0;
        ttrial_total_time[0] = 0;
        ttrial_total_time[1] = 0;
        ttrial_total_time[2] = 0;
        ttrial_show_total = false;
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

    // check_stage() has already advanced current_lap by the time this wrapper
    // runs. Detect that edge and capture the just-finished lap from laptimes[].
    if (outrun.cannonball_mode == Outrun::MODE_TTRIAL)
    {
        const int current_lap = static_cast<int>(outrun.ttrial.current_lap);

        if (last_ttrial_lap < 0 || current_lap < last_ttrial_lap)
        {
            last_ttrial_lap = current_lap;
        }
        else if (current_lap > last_ttrial_lap)
        {
            begin_ttrial_lap_banner(current_lap - 1, lap_ms);
            last_ttrial_lap = current_lap;
        }
    }
    else if (last_ttrial_lap != -1 || ttrial_lap_banner_ticks != 0)
    {
        reset_ttrial_lap_tracking();
    }

    do_timers_base();

    if (endless_ingame)
    {
        // Remove the original LAP TIME checkpoint overlay every frame while
        // Endless owns the transition presentation. Original/Continuous keep
        // their stock behaviour because this branch is Endless-only.
        if (extend_play_timer)
            clear_endless_laptime();

        draw_endless_banner();
    }

    // Traffic OFF Time Trial is purely a lap-time challenge. Keep the normal
    // score HUD out of the way while driving that class.
    clear_ttrial_no_traffic_score();

    // Draw after the preserved timer code so the lap notification always owns
    // its temporary centre-screen area, including on the final lap as GOAL starts.
    if (outrun.cannonball_mode == Outrun::MODE_TTRIAL)
        tick_ttrial_lap_banner();
}

void OStats::init_next_level()
{
    const bool endless_checkpoint =
        outrun.endless_mode &&
        outrun.cannonball_mode == Outrun::MODE_CONT &&
        outrun.game_state == GS_INGAME &&
        extend_play_timer == 0 &&
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