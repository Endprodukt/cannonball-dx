/***************************************************************************
    Best Outrunners - CannonBall DX score-screen wrapper.

    The original score implementation is preserved in ohiscore_base.cpp.
    Endless and Time Trial substitute their dedicated tables only at game end.
    DX also rotates the attract-mode score display through Original, Original
    Japan, Continuous, Endless and a Time Trial course-record overview while
    preserving the stock five-second timing and mini-car reveal animation.
***************************************************************************/

// Pre-include the preserved implementation's dependencies before temporarily
// renaming OHiScore methods. This keeps generic tokens such as init/tick away
// from unrelated declarations in those headers.
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
        ATTRACT_SCORE_TIME_TRIAL,
        ATTRACT_SCORE_PAGE_COUNT
    };

    const int ATTRACT_SCORE_SECONDS = 5;

    bool endless_score_audio_started = false;
    bool time_trial_score_audio_started = false;

    bool attract_score_rotation_active = false;
    int attract_score_page = ATTRACT_SCORE_ORIGINAL;
    int attract_saved_mode = Outrun::MODE_ORIGINAL;
    int attract_saved_jap = 0;
    std::string attract_time_trial_footer;

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

    void stabilize_score_background()
    {
        // Dedicated game-end score screens can be entered from any stage, and
        // Time Trial in particular normally leaves the selected course's tilemap
        // and palette live. Rebuild the complete common Best OutRunners scene
        // for those dedicated screens only.
        //
        // IMPORTANT: never call this for GS_INIT_BEST1/GS_BEST1. The original
        // attract-mode high-score sequence is an organic overlay on the demo
        // scene that was already running; resetting road/tile/palette state here
        // makes the road disappear and visibly jumps to a different background.
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

        // These are the original dedicated Best OutRunners palette overrides:
        // shaded red/sunset backdrop plus the black score-screen road.
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

    void clear_score_tile_page()
    {
        uint32_t tile_addr = 0x10E000;
        for (int i = 0; i <= 0x3FF; i++)
            video.write_tile32(&tile_addr, 0x200020);
    }

    void write_attract_tile_row(int row, const std::string& source)
    {
        // The stock mini-car routine reveals seven two-line score bands. The
        // visible System 16 name table starts at hardware column 24, and the
        // stock first score row is y=8 with subsequent rows two lines apart.
        uint32_t dst = ohud.translate(
            24,
            static_cast<uint16_t>(8 + (row * 2)),
            0x10E000);

        for (int x = 0; x < 40; x++)
        {
            const char value = x < static_cast<int>(source.size()) ?
                source[x] : ' ';
            video.write_tile8(dst + 1, attract_tile_char(value));
            dst += 2;
        }
    }

    int bcd_to_decimal(uint8_t value)
    {
        return ((value >> 4) * 10) + (value & 0x0F);
    }

    void format_counter(uint16_t counter, char* dst, size_t size)
    {
        if (!counter)
        {
            std::snprintf(dst, size, "--'--\"--");
            return;
        }

        uint8_t converted[3] = {0, 0, 0};
        outils::convert_counter_to_time(counter, converted);

        std::snprintf(
            dst,
            size,
            "%02u'%02u\"%02u",
            static_cast<unsigned>(converted[0]),
            static_cast<unsigned>(bcd_to_decimal(converted[1])),
            static_cast<unsigned>(bcd_to_decimal(converted[2])));
    }

    void format_endless_time(uint32_t ticks, char* dst, size_t size)
    {
        if (!ticks)
        {
            std::snprintf(dst, size, "--'--\"--");
            return;
        }

        const uint64_t centiseconds =
            (static_cast<uint64_t>(ticks) * 100ULL) / 60ULL;
        const uint32_t minutes =
            static_cast<uint32_t>(centiseconds / 6000ULL);
        const uint32_t seconds =
            static_cast<uint32_t>((centiseconds / 100ULL) % 60ULL);
        const uint32_t hundredths =
            static_cast<uint32_t>(centiseconds % 100ULL);

        std::snprintf(
            dst,
            size,
            "%02u'%02u\"%02u",
            minutes,
            seconds,
            hundredths);
    }

    char read_initial(xml_parser::ptree& data, const std::string& key)
    {
        const std::string value = data.get_string(key, "_");
        return value.empty() || value[0] == '_' ? ' ' : value[0];
    }

    void prepare_endless_attract_rows()
    {
        xml_parser::ptree data("endless_scores");
        xml_parser::read_xml(
            config.data.save_path + "hiscores_endless.xml",
            data);

        for (int row = 0; row < 7; row++)
        {
            const std::string tag = "score" + Utils::to_string(row);
            const uint16_t stages = static_cast<uint16_t>(
                data.get_int(tag + ".stages", 0));
            const uint32_t distance = static_cast<uint32_t>(
                data.get_int(tag + ".distance_tenths", 0));
            const uint32_t ticks = static_cast<uint32_t>(
                data.get_int(tag + ".time_ticks", 0));

            std::string line;
            if (stages || distance || ticks)
            {
                const char i1 = read_initial(data, tag + ".initial1");
                const char i2 = read_initial(data, tag + ".initial2");
                const char i3 = read_initial(data, tag + ".initial3");

                char time_text[16];
                format_endless_time(ticks, time_text, sizeof(time_text));

                char row_text[64];
                std::snprintf(
                    row_text,
                    sizeof(row_text),
                    "%2d %c%c%c %3u STG %5u.%u KM %8s",
                    row + 1,
                    i1,
                    i2,
                    i3,
                    static_cast<unsigned>(stages),
                    static_cast<unsigned>(distance / 10),
                    static_cast<unsigned>(distance % 10),
                    time_text);
                line = row_text;
            }

            write_attract_tile_row(row, line);
        }
    }

    const char* time_trial_short_name(int track)
    {
        static const char* NAMES[TimeTrialRecords::TRACK_COUNT] =
        {
            "COCONUT", "GATEWAY", "DEVILS",  "DESERT",  "ALPS",
            "CLOUDY",  "WILDRNS", "OLD CAP", "WHEAT",   "SEASIDE",
            "VINEYRD", "DEATH V", "DESOLAT", "AUTOBAH", "LAKESID"
        };

        return track >= 0 && track < TimeTrialRecords::TRACK_COUNT ?
            NAMES[track] : "UNKNOWN";
    }

    std::string time_trial_record_cell(xml_parser::ptree& data, int track)
    {
        const std::string track_base =
            "time_trial.track" + Utils::to_string(track) + ".traffic_on.entry0";
        const std::string legacy_base =
            "time_trial.record" + Utils::to_string(track);

        uint16_t total = static_cast<uint16_t>(
            data.get_int(track_base + ".total", 0));
        std::string initial_base = track_base;

        if (!total)
        {
            total = static_cast<uint16_t>(
                data.get_int(legacy_base + ".total", 0));
            initial_base = legacy_base;
        }

        const char i1 = total ? read_initial(data, initial_base + ".initial1") : '-';
        const char i2 = total ? read_initial(data, initial_base + ".initial2") : '-';
        const char i3 = total ? read_initial(data, initial_base + ".initial3") : '-';

        char time_text[16];
        format_counter(total, time_text, sizeof(time_text));

        char cell[32];
        std::snprintf(
            cell,
            sizeof(cell),
            "%-7s %c%c%c %8s",
            time_trial_short_name(track),
            i1,
            i2,
            i3,
            time_text);
        return std::string(cell);
    }

    void prepare_time_trial_attract_rows()
    {
        xml_parser::ptree data("timetrial_scores");
        xml_parser::read_xml(config.data.file_ttrial, data);

        // Fourteen course records are revealed in two columns by the seven
        // original mini-cars. The fifteenth course sits centered underneath;
        // this keeps every course on one five-second attract page.
        for (int row = 0; row < 7; row++)
        {
            std::string line = time_trial_record_cell(data, row);
            line += time_trial_record_cell(data, row + 7);
            write_attract_tile_row(row, line);
        }

        attract_time_trial_footer = time_trial_record_cell(data, 14);
    }

    void draw_custom_attract_header(int page)
    {
        if (page == ATTRACT_SCORE_ENDLESS)
        {
            draw_attract_centered(1, "ENDLESS OUTRUNNERS", OHud::GREEN);
            draw_attract_text(
                0,
                5,
                "# NAME STAGES      DISTANCE       TIME",
                OHud::GREY);
        }
        else if (page == ATTRACT_SCORE_TIME_TRIAL)
        {
            draw_attract_centered(1, "TIME TRIAL RECORDS", OHud::GREEN);
            draw_attract_centered(
                4,
                "TRAFFIC ON - BEST BY COURSE",
                OHud::GREY);
            draw_attract_text(
                10,
                23,
                attract_time_trial_footer,
                OHud::GREY);
        }
    }

    void draw_stock_attract_label(int page)
    {
        const char* label = "ORIGINAL";

        if (page == ATTRACT_SCORE_ORIGINAL_JAPAN)
            label = "ORIGINAL JAPAN";
        else if (page == ATTRACT_SCORE_CONTINUOUS)
            label = "CONTINUOUS";

        draw_attract_centered(4, label, OHud::GREEN);
    }

    void restore_attract_runtime()
    {
        config.engine.jap = attract_saved_jap;
        outrun.cannonball_mode = attract_saved_mode;
        attract_score_rotation_active = false;
        attract_time_trial_footer.clear();
    }

    void configure_attract_page(OHiScore& score, int page)
    {
        attract_score_page = page;
        attract_time_trial_footer.clear();

        // Every page starts a fresh copy of the original mini-car state machine.
        score.init_base();

        switch (page)
        {
            case ATTRACT_SCORE_ORIGINAL:
                config.engine.jap = 0;
                outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
                score.init_def_scores();
                config.load_scores(true);
                break;

            case ATTRACT_SCORE_ORIGINAL_JAPAN:
                config.engine.jap = 1;
                outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
                score.init_def_scores();
                config.load_scores(true);
                break;

            case ATTRACT_SCORE_CONTINUOUS:
                config.engine.jap = 0;
                outrun.cannonball_mode = Outrun::MODE_CONT;
                score.init_def_scores();
                config.load_scores(false);
                break;

            case ATTRACT_SCORE_ENDLESS:
                config.engine.jap = 0;
                outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
                break;

            case ATTRACT_SCORE_TIME_TRIAL:
                config.engine.jap = 0;
                outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
                break;
        }
    }

    bool attract_timer_will_expire_this_tick()
    {
        // GS_BEST1 calls display_scores() immediately before decrement_timers().
        // Mirror the two stock timer branches so the next DX page is installed
        // only on the exact frame on which the original screen would have ended.
        if (config.engine.fix_timer)
            return ostats.time_counter == 1 && ostats.frame_counter <= 1;

        return ostats.time_counter == 0 && ostats.frame_counter <= 0;
    }
}

void OHiScore::init()
{
    // Preserve the original attract transition exactly: BEST1 must inherit the
    // running demo's road, scenery and palette instead of rebuilding a separate
    // Best OutRunners background. DX only rotates the score data presented on
    // top of that untouched scene.
    if (attract_score_screen())
    {
        attract_saved_jap = config.engine.jap;
        attract_saved_mode = outrun.cannonball_mode;
        attract_score_rotation_active = true;
        configure_attract_page(*this, ATTRACT_SCORE_ORIGINAL);
        return;
    }

    stabilize_score_background();

    if (time_trial_record_screen())
    {
        time_trial_score_audio_started = false;
        time_trial_records.init_screen();
        return;
    }

    if (!endless_gameover_score_screen())
    {
        init_base();
        return;
    }

    endless_score_audio_started = false;
    endless_hiscore.capture_result(
        outrun.endless_stage,
        ostats.score);

    endless_hiscore.init_screen();
}

void OHiScore::display_scores()
{
    if (!attract_score_rotation_active || !attract_score_screen())
    {
        display_scores_base();
        return;
    }

    if (attract_score_page <= ATTRACT_SCORE_CONTINUOUS)
    {
        display_scores_base();
        draw_stock_attract_label(attract_score_page);
    }
    else
    {
        switch (best_or_state)
        {
            case 0:
                video.clear_text_ram();
                clear_score_tile_page();
                setup_minicars();

                if (attract_score_page == ATTRACT_SCORE_ENDLESS)
                    prepare_endless_attract_rows();
                else
                    prepare_time_trial_attract_rows();

                best_or_state = 1;
                break;

            case 1:
                tick_minicars();
                if (dest_total >= 7)
                    best_or_state = 2;
                break;

            case 2:
                break;
        }

        draw_custom_attract_header(attract_score_page);
    }

    // Credits can leave BEST1 immediately. Restore the player's configured
    // region/mode before the Music Select state is entered.
    if (ostats.credits)
    {
        restore_attract_runtime();
        return;
    }

    if (!attract_timer_will_expire_this_tick())
        return;

    if (attract_score_page + 1 < ATTRACT_SCORE_PAGE_COUNT)
    {
        configure_attract_page(*this, attract_score_page + 1);
        ostats.time_counter = ATTRACT_SCORE_SECONDS;
        ostats.frame_counter = ostats.frame_reset;
    }
    else
    {
        // Leave the final timer untouched. The stock GS_BEST1 call to
        // decrement_timers() directly after this function will expire normally
        // and continue to GS_INIT_LOGO, exactly as before the rotation existed.
        restore_attract_runtime();
    }
}

void OHiScore::tick()
{
    if (time_trial_record_screen())
    {
        // GS_INIT_BEST2 resets FM/WAV immediately after init(). Start the
        // familiar Last Wave high-score ambience on the first real BEST2 tick.
        if (!time_trial_score_audio_started && outrun.game_state == GS_BEST2)
        {
            osoundint.queue_sound(sound::PCM_WAVE);
            osoundint.queue_sound(sound::MUSIC_LASTWAVE);
            time_trial_score_audio_started = true;
        }

        time_trial_records.tick_screen();
        return;
    }

    if (endless_gameover_score_screen())
    {
        // GS_INIT_BEST2 queues FM_RESET and clears WAV playback immediately
        // after init(). Start Last Wave on the first real BEST2 tick instead,
        // after that stock reset has completed, so the Endless score screen
        // gets the intended OutRun high-score music and sea ambience.
        if (!endless_score_audio_started && outrun.game_state == GS_BEST2)
        {
            osoundint.queue_sound(sound::PCM_WAVE);
            osoundint.queue_sound(sound::MUSIC_LASTWAVE);
            endless_score_audio_started = true;
        }

        endless_hiscore.tick_screen();
        return;
    }

    tick_base();
}

int OHiScore::score_position()
{
    if (time_trial_record_screen())
    {
        time_trial_records.finish_flow();

        // GS_BEST2 performs the normal full engine reset immediately after
        // this call. Switch back to Original first so it initializes Stage 1
        // and returns to attract mode instead of reloading the Time Trial track.
        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        time_trial_score_audio_started = false;
        return -1;
    }

    // The preserved GS_BEST2 exit saves Original/Continuous tables when this
    // reports a valid entry. Endless persists its own XML table, so suppress
    // that legacy save and keep hiscores_continuous.xml untouched.
    if (endless_gameover_score_screen())
    {
        endless_hiscore.save_if_needed();

        // GS_BEST2 immediately performs the full engine reset after this call.
        // Return the runtime to the normal Original attract state first so an
        // Endless MODE_CONT flag cannot leak into the demo/audio sequence.
        outrun.endless_mode = false;
        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        endless_score_audio_started = false;
        return -1;
    }

    return score_position_base();
}
