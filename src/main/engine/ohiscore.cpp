/***************************************************************************
    Best Outrunners - CannonBall DX score-screen wrapper.

    The original score implementation is preserved in ohiscore_base.cpp.
    Endless and Time Trial substitute their dedicated tables only at game end.
    DX also rotates the attract-mode score display through Original, Original
    Japan, Continuous, Endless and a two-page Time Trial course-record overview
    while preserving the stock five-second timing and mini-car reveal animation.
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
        ATTRACT_SCORE_TIME_TRIAL_1,
        ATTRACT_SCORE_TIME_TRIAL_2,
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

    bool attract_time_trial_page(int page)
    {
        return page == ATTRACT_SCORE_TIME_TRIAL_1 ||
               page == ATTRACT_SCORE_TIME_TRIAL_2;
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
            video.write_text16(&dst, (colour << 8) | attract_tile_char(value));
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
        uint32_t dst = ohud.translate(
            24,
            static_cast<uint16_t>(8 + (row * 2)),
            0x10E000);

        for (int x = 0; x < 40; x++)
        {
            const char value = x < static_cast<int>(source.size()) ? source[x] : ' ';
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
        std::snprintf(dst, size, "%02u'%02u\"%02u",
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

        const uint64_t centiseconds = (static_cast<uint64_t>(ticks) * 100ULL) / 60ULL;
        const uint32_t minutes = static_cast<uint32_t>(centiseconds / 6000ULL);
        const uint32_t seconds = static_cast<uint32_t>((centiseconds / 100ULL) % 60ULL);
        const uint32_t hundredths = static_cast<uint32_t>(centiseconds % 100ULL);

        std::snprintf(dst, size, "%02u'%02u\"%02u", minutes, seconds, hundredths);
    }

    char read_initial(xml_parser::ptree& data, const std::string& key)
    {
        const std::string value = data.get_string(key, "_");
        return value.empty() || value[0] == '_' ? ' ' : value[0];
    }

    void prepare_endless_attract_rows()
    {
        xml_parser::ptree data("endless_scores");
        xml_parser::read_xml(config.data.save_path + "hiscores_endless.xml", data);

        for (int row = 0; row < 7; row++)
        {
            const std::string tag = "score" + Utils::to_string(row);
            const uint16_t stages = static_cast<uint16_t>(data.get_int(tag + ".stages", 0));
            const uint32_t distance = static_cast<uint32_t>(data.get_int(tag + ".distance_tenths", 0));
            const uint32_t ticks = static_cast<uint32_t>(data.get_int(tag + ".time_ticks", 0));

            std::string line;
            if (stages || distance || ticks)
            {
                const char i1 = read_initial(data, tag + ".initial1");
                const char i2 = read_initial(data, tag + ".initial2");
                const char i3 = read_initial(data, tag + ".initial3");
                char time_text[16];
                format_endless_time(ticks, time_text, sizeof(time_text));

                char row_text[64];
                std::snprintf(row_text, sizeof(row_text),
                    "%2d %c%c%c %3u STG %5u.%u KM %8s",
                    row + 1, i1, i2, i3,
                    static_cast<unsigned>(stages),
                    static_cast<unsigned>(distance / 10),
                    static_cast<unsigned>(distance % 10),
                    time_text);
                line = row_text;
            }
            write_attract_tile_row(row, line);
        }
    }

    const char* time_trial_name(int track)
    {
        static const char* NAMES[TimeTrialRecords::TRACK_COUNT] =
        {
            "COCONUT BEACH", "GATEWAY", "DEVILS CANYON", "DESERT", "ALPS",
            "CLOUDY MOUNTAIN", "WILDERNESS", "OLD CAPITAL", "WHEAT FIELD",
            "SEASIDE TOWN", "VINEYARD", "DEATH VALLEY", "DESOLATION HILL",
            "AUTOBAHN", "LAKESIDE"
        };
        return track >= 0 && track < TimeTrialRecords::TRACK_COUNT ? NAMES[track] : "UNKNOWN";
    }

    std::string time_trial_record_cell(xml_parser::ptree& data, int track)
    {
        const std::string track_base =
            "time_trial.track" + Utils::to_string(track) + ".traffic_on.entry0";
        const std::string legacy_base = "time_trial.record" + Utils::to_string(track);

        uint16_t total = static_cast<uint16_t>(data.get_int(track_base + ".total", 0));
        std::string initial_base = track_base;
        if (!total)
        {
            total = static_cast<uint16_t>(data.get_int(legacy_base + ".total", 0));
            initial_base = legacy_base;
        }

        const char i1 = total ? read_initial(data, initial_base + ".initial1") : '-';
        const char i2 = total ? read_initial(data, initial_base + ".initial2") : '-';
        const char i3 = total ? read_initial(data, initial_base + ".initial3") : '-';
        char time_text[16];
        format_counter(total, time_text, sizeof(time_text));

        char cell[40];
        std::snprintf(cell, sizeof(cell), "%-16s %c%c%c %8s",
            time_trial_name(track), i1, i2, i3, time_text);
        return std::string(cell);
    }

    void prepare_time_trial_attract_rows(int page)
    {
        xml_parser::ptree data("timetrial_scores");
        xml_parser::read_xml(config.data.file_ttrial, data);
        const int first_track = page == ATTRACT_SCORE_TIME_TRIAL_1 ? 0 : 7;

        for (int row = 0; row < 7; row++)
            write_attract_tile_row(row, time_trial_record_cell(data, first_track + row));

        if (page == ATTRACT_SCORE_TIME_TRIAL_2)
            attract_time_trial_footer = time_trial_record_cell(data, 14);
    }

    const char* attract_page_name(int page)
    {
        switch (page)
        {
            case ATTRACT_SCORE_ORIGINAL:       return "ORIGINAL";
            case ATTRACT_SCORE_ORIGINAL_JAPAN: return "ORIGINAL JAPAN";
            case ATTRACT_SCORE_CONTINUOUS:     return "CONTINUOUS";
            case ATTRACT_SCORE_ENDLESS:        return "ENDLESS";
            case ATTRACT_SCORE_TIME_TRIAL_1:   return "TIME TRIAL 1/2";
            case ATTRACT_SCORE_TIME_TRIAL_2:   return "TIME TRIAL 2/2";
            default:                           return "";
        }
    }

    void draw_stock_score_header_shifted()
    {
        uint32_t src = TEXT1_SCORE_ETC;
        uint32_t original_dst = roms.rom0.read32(&src);
        const uint16_t counter = roms.rom0.read16(&src);
        uint16_t data = roms.rom0.read16(&src);

        uint32_t clear_dst = original_dst;
        for (uint16_t i = 0; i <= counter; i++)
            video.write_text16(&clear_dst, 0);

        uint32_t shifted_dst = original_dst + 0x80;
        for (uint16_t i = 0; i <= counter; i++)
        {
            data = (data & 0xFF00) | roms.rom0.read8(&src);
            video.write_text16(&shifted_dst, data);
        }
    }

    void draw_attract_page_header(int page)
    {
        ohud.blit_text2(TEXT2_BEST_OR);
        draw_attract_centered(5, attract_page_name(page), OHud::GREEN);

        if (page == ATTRACT_SCORE_ENDLESS)
        {
            draw_attract_text(0, 6,
                "# NAME STAGES      DISTANCE       TIME", OHud::GREY);
        }
        else if (attract_time_trial_page(page))
        {
            draw_attract_centered(6, "TRAFFIC ON - BEST BY COURSE", OHud::GREY);
            if (page == ATTRACT_SCORE_TIME_TRIAL_2 && !attract_time_trial_footer.empty())
                draw_attract_text(5, 23, attract_time_trial_footer, OHud::GREY);
        }
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
        score.init_base();

        const int runtime_jap = config.engine.jap;
        switch (page)
        {
            case ATTRACT_SCORE_ORIGINAL:
                config.engine.jap = 0;
                score.init_def_scores();
                config.load_scores(true);
                config.engine.jap = runtime_jap;
                break;
            case ATTRACT_SCORE_ORIGINAL_JAPAN:
                config.engine.jap = 1;
                score.init_def_scores();
                config.load_scores(true);
                config.engine.jap = runtime_jap;
                break;
            case ATTRACT_SCORE_CONTINUOUS:
                config.engine.jap = 0;
                score.init_def_scores();
                config.load_scores(false);
                config.engine.jap = runtime_jap;
                break;
            case ATTRACT_SCORE_ENDLESS:
            case ATTRACT_SCORE_TIME_TRIAL_1:
            case ATTRACT_SCORE_TIME_TRIAL_2:
                break;
        }
    }

    bool attract_timer_will_expire_this_tick()
    {
        if (config.engine.fix_timer)
            return ostats.time_counter == 1 && ostats.frame_counter <= 1;
        return ostats.time_counter == 0 && ostats.frame_counter <= 0;
    }
}

void OHiScore::init()
{
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
    endless_hiscore.capture_result(outrun.endless_stage, ostats.score);
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
        const int runtime_mode = outrun.cannonball_mode;
        outrun.cannonball_mode = attract_score_page == ATTRACT_SCORE_CONTINUOUS ?
            Outrun::MODE_CONT : Outrun::MODE_ORIGINAL;

        display_scores_base();
        outrun.cannonball_mode = runtime_mode;
        draw_stock_score_header_shifted();
        draw_attract_page_header(attract_score_page);
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
                    prepare_time_trial_attract_rows(attract_score_page);
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
        draw_attract_page_header(attract_score_page);
    }

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
        restore_attract_runtime();
    }
}

void OHiScore::tick()
{
    if (time_trial_record_screen())
    {
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
        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        time_trial_score_audio_started = false;
        return -1;
    }

    if (endless_gameover_score_screen())
    {
        endless_hiscore.save_if_needed();
        outrun.endless_mode = false;
        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        endless_score_audio_started = false;
        return -1;
    }
    return score_position_base();
}
