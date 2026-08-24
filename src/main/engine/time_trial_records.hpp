/***************************************************************************
    CannonBall DX Time Trial Course Records.

    Each of the 15 Time Trial courses owns two independent 20-entry tables:
    Traffic ON and Traffic OFF. Everything remains in the existing Time Trial
    XML file so the data can later be reused by an attract-mode overview.
***************************************************************************/

#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../utils.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/oroad.hpp"
#include "engine/ostats.hpp"
#include "engine/outils.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "sdl2/input.hpp"

class TimeTrialRecords
{
public:
    static const int TRACK_COUNT = 15;
    static const int RECORD_LAPS = 3;
    static const int TABLE_ENTRIES = 20;

    struct Record
    {
        uint16_t total_counter = 0;
        uint16_t best_lap_counter = 0;
        uint16_t overtakes = 0;
        uint16_t vehicle_cols = 0;
        uint16_t crashes = 0;
        char initial1 = ' ';
        char initial2 = ' ';
        char initial3 = ' ';
    };

    // The preserved engine still asks whether START is held on the old Time
    // Trial GAME OVER screen. The Outrun wrapper redirects that exact query
    // through these helpers and advances automatically after the Results page.
    bool suppress_physical_input(Input::presses press) const
    {
        return press == Input::START &&
               outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
               outrun.game_state == GS_GAMEOVER;
    }

    bool synthetic_input(Input::presses press)
    {
        if (press != Input::START ||
            outrun.cannonball_mode != Outrun::MODE_TTRIAL ||
            outrun.game_state != GS_GAMEOVER)
        {
            return false;
        }

        if (!results_active)
            begin_results();

        render_results();

        ++results_ticks;
        return results_ticks >= RESULTS_TICKS;
    }

    void begin_records_transition()
    {
        results_active = false;
        results_ticks = 0;
        record_screen_active = false;
    }

    void init_screen()
    {
        if (!run_captured)
            capture_run();

        record_screen_active = true;
        initials_done = false;
        initial_selected = 0;
        letter_selected = 0;
        steering_repeat = 0;

        // The accelerator may still be held after crossing the finish line.
        // Require a release/re-press before accepting the first initial.
        accel_old =
            oinputs.input_acc >= 0x60 || input.is_pressed(Input::ACCEL);

        if (qualifying_score)
        {
            ostats.time_counter = config.engine.hiscore_timer;
            save(); // Preserve the new row even if initials entry times out.
        }
        else
        {
            // Non-qualifying runs still get enough time to read the table for
            // the course and traffic class that was just driven.
            ostats.time_counter = 8;
        }
        ostats.frame_counter = ostats.frame_reset;

        // The stock Best OutRunners table is tile based. Clear its page so the
        // dedicated Time Trial text table is never drawn over stale score data.
        uint32_t tile_addr = 0x10E000;
        for (int i = 0; i <= 0x3FF; i++)
            video.write_tile32(&tile_addr, 0x200020);

        video.clear_text_ram();
        video.enabled = true;
        render_records();
    }

    void tick_screen()
    {
        render_records();

        if (!qualifying_score || initials_done)
            return;

        update_letter_selection();

        const bool accel_now =
            oinputs.input_acc >= 0x60 || input.is_pressed(Input::ACCEL);

        if (accel_now && !accel_old)
            accept_letter();

        accel_old = accel_now;
    }

    void save_if_needed()
    {
        if (run_captured)
            save();
    }

    void finish_flow()
    {
        save_if_needed();

        // The legacy frontend used this flag to save the best lap when Time
        // Trial returned to the menu. This flow persists its own tables.
        outrun.ttrial.new_high_score = false;

        results_active = false;
        results_ticks = 0;
        record_screen_active = false;
        run_captured = false;
        qualifying_score = false;
        new_course_record = false;
        current_track = -1;
        current_traffic_class = TRAFFIC_ON;
        score_pos = -1;
        initials_done = false;
        initial_selected = 0;
    }

    bool is_record_screen_active() const
    {
        return record_screen_active;
    }

private:
    static const int RESULTS_TICKS = 30 * 7;
    static const int TRAFFIC_CLASSES = 2;
    static const int TRAFFIC_OFF = 0;
    static const int TRAFFIC_ON = 1;

    using ScoreTable = std::array<Record, TABLE_ENTRIES>;

    // [track][traffic class][position]
    std::array<std::array<ScoreTable, TRAFFIC_CLASSES>, TRACK_COUNT> records{};

    // Absolute best single lap, independently tracked for each course/class.
    std::array<std::array<uint16_t, TRAFFIC_CLASSES>, TRACK_COUNT> fastest_laps{};

    Record pending{};

    bool results_active = false;
    int results_ticks = 0;
    bool record_screen_active = false;
    bool run_captured = false;
    bool qualifying_score = false;
    bool new_course_record = false;
    int current_track = -1;
    int current_traffic_class = TRAFFIC_ON;
    int score_pos = -1;

    bool initials_done = false;
    int initial_selected = 0;
    int letter_selected = 0;
    int steering_repeat = 0;
    bool accel_old = false;

    static int bcd_to_decimal(uint8_t value)
    {
        return ((value >> 4) * 10) + (value & 0x0F);
    }

    static int track_from_level(uint8_t level)
    {
        static const uint8_t LEVELS[TRACK_COUNT] =
        {
            0x00,
            0x09, 0x08,
            0x12, 0x11, 0x10,
            0x1B, 0x1A, 0x19, 0x18,
            0x24, 0x23, 0x22, 0x21, 0x20
        };

        for (int i = 0; i < TRACK_COUNT; i++)
        {
            if (LEVELS[i] == level)
                return i;
        }

        return -1;
    }

    static const char* track_name(int index)
    {
        switch (index)
        {
            case 0:  return "COCONUT BEACH";
            case 1:  return config.engine.jap ? "WHEAT FIELD" : "GATEWAY";
            case 2:  return config.engine.jap ? "CLOUDY MOUNTAIN" : "DEVILS CANYON";
            case 3:  return "DESERT";
            case 4:  return "ALPS";
            case 5:  return config.engine.jap ? "DEVILS CANYON" : "CLOUDY MOUNTAIN";
            case 6:  return "WILDERNESS";
            case 7:  return "OLD CAPITAL";
            case 8:  return config.engine.jap ? "GATEWAY" : "WHEAT FIELD";
            case 9:  return "SEASIDE TOWN";
            case 10: return "VINEYARD";
            case 11: return "DEATH VALLEY";
            case 12: return "DESOLATION HILL";
            case 13: return "AUTOBAHN";
            case 14: return "LAKESIDE";
            default: return "UNKNOWN";
        }
    }

    static const char* traffic_name(int traffic_class)
    {
        return traffic_class == TRAFFIC_ON ? "TRAFFIC ON" : "TRAFFIC OFF";
    }

    std::string filename() const
    {
        return config.engine.jap ?
            config.data.file_ttrial_jap : config.data.file_ttrial;
    }

    static std::string class_tag(int track, int traffic_class)
    {
        return "time_trial.track" + Utils::to_string(track) +
            (traffic_class == TRAFFIC_ON ? ".traffic_on" : ".traffic_off");
    }

    static void read_record(xml_parser::ptree& data,
                            const std::string& tag,
                            Record& record)
    {
        record.total_counter = static_cast<uint16_t>(
            data.get_int(tag + ".total", 0));
        record.best_lap_counter = static_cast<uint16_t>(
            data.get_int(tag + ".best_lap", 0));
        record.overtakes = static_cast<uint16_t>(
            data.get_int(tag + ".overtakes", 0));
        record.vehicle_cols = static_cast<uint16_t>(
            data.get_int(tag + ".vehicle_cols", 0));
        record.crashes = static_cast<uint16_t>(
            data.get_int(tag + ".crashes", 0));

        const std::string i1 = data.get_string(tag + ".initial1", "_");
        const std::string i2 = data.get_string(tag + ".initial2", "_");
        const std::string i3 = data.get_string(tag + ".initial3", "_");

        record.initial1 = i1.empty() || i1[0] == '_' ? ' ' : i1[0];
        record.initial2 = i2.empty() || i2[0] == '_' ? ' ' : i2[0];
        record.initial3 = i3.empty() || i3[0] == '_' ? ' ' : i3[0];
    }

    static void write_record(xml_parser::ptree& data,
                             const std::string& tag,
                             const Record& record)
    {
        data.put_int(tag + ".total", record.total_counter);
        data.put_int(tag + ".best_lap", record.best_lap_counter);
        data.put_int(tag + ".overtakes", record.overtakes);
        data.put_int(tag + ".vehicle_cols", record.vehicle_cols);
        data.put_int(tag + ".crashes", record.crashes);
        data.put_string(tag + ".initial1",
            record.initial1 == ' ' ? "_" : std::string(1, record.initial1));
        data.put_string(tag + ".initial2",
            record.initial2 == ' ' ? "_" : std::string(1, record.initial2));
        data.put_string(tag + ".initial3",
            record.initial3 == ' ' ? "_" : std::string(1, record.initial3));
    }

    void begin_results()
    {
        capture_run();
        results_active = true;
        results_ticks = 0;
        video.clear_text_ram();
    }

    int insert_position(const ScoreTable& table, uint16_t total) const
    {
        if (!total)
            return -1;

        for (int i = 0; i < TABLE_ENTRIES; i++)
        {
            if (!table[i].total_counter || total < table[i].total_counter)
                return i;
        }

        return -1;
    }

    void capture_run()
    {
        if (run_captured)
            return;

        load();

        current_track = track_from_level(outrun.ttrial.level);
        current_traffic_class = outrun.ttrial.traffic ? TRAFFIC_ON : TRAFFIC_OFF;
        pending = Record{};
        qualifying_score = false;
        new_course_record = false;
        score_pos = -1;

        const int laps = std::min<int>(outrun.ttrial.laps, 5);
        uint32_t total = 0;
        uint16_t best = 0;

        for (int lap = 0; lap < laps; lap++)
        {
            const int counter_signed = ostats.stage_counters[lap];
            if (counter_signed <= 0)
                continue;

            const uint16_t counter = static_cast<uint16_t>(counter_signed);
            total += counter;

            if (!best || counter < best)
                best = counter;
        }

        if (total > 0xFFFF)
            total = 0xFFFF;

        pending.total_counter = static_cast<uint16_t>(total);
        pending.best_lap_counter = best;
        pending.overtakes = outrun.ttrial.overtakes;
        pending.vehicle_cols = outrun.ttrial.vehicle_cols;
        pending.crashes = outrun.ttrial.crashes;

        if (current_track >= 0)
        {
            uint16_t& class_best =
                fastest_laps[current_track][current_traffic_class];

            if (best && (!class_best || best < class_best))
                class_best = best;

            // The legacy single-lap slots represent the traditional Traffic ON
            // class. Keep them synchronized for old builds and XML readers.
            if (current_traffic_class == TRAFFIC_ON && class_best)
                config.ttrial.best_times[current_track] = class_best;

            // Course leaderboards are deliberately comparable: exactly three
            // completed laps are required. Other configured lap counts can still
            // update their class-specific fastest single lap.
            if (laps == RECORD_LAPS && pending.total_counter)
            {
                ScoreTable& table =
                    records[current_track][current_traffic_class];
                score_pos = insert_position(table, pending.total_counter);

                if (score_pos >= 0)
                {
                    for (int i = TABLE_ENTRIES - 1; i > score_pos; --i)
                        table[i] = table[i - 1];

                    table[score_pos] = pending;
                    qualifying_score = true;
                    new_course_record = score_pos == 0;
                }
            }

            save();
        }

        // Menu::init() must not perform the old best-lap-only save later.
        outrun.ttrial.new_high_score = false;
        run_captured = true;
    }

    void load()
    {
        for (auto& track_tables : records)
            for (auto& table : track_tables)
                for (auto& record : table)
                    record = Record{};

        for (auto& track_best : fastest_laps)
            for (auto& best : track_best)
                best = 0;

        xml_parser::ptree data("timetrial_scores");
        if (!xml_parser::read_xml(filename(), data))
            return;

        for (int track = 0; track < TRACK_COUNT; track++)
        {
            for (int traffic_class = 0; traffic_class < TRAFFIC_CLASSES; traffic_class++)
            {
                const std::string base = class_tag(track, traffic_class);
                fastest_laps[track][traffic_class] = static_cast<uint16_t>(
                    data.get_int(base + ".fastest_lap", 0));

                for (int pos = 0; pos < TABLE_ENTRIES; pos++)
                {
                    read_record(
                        data,
                        base + ".entry" + Utils::to_string(pos),
                        records[track][traffic_class][pos]);
                }
            }

            // Migrate the old single best-lap and one-record format into the
            // traditional Traffic ON class. No extra XML files are created.
            if (!fastest_laps[track][TRAFFIC_ON])
            {
                fastest_laps[track][TRAFFIC_ON] = static_cast<uint16_t>(
                    data.get_int(
                        "time_trial.score" + Utils::to_string(track),
                        0));
            }

            if (!records[track][TRAFFIC_ON][0].total_counter)
            {
                read_record(
                    data,
                    "time_trial.record" + Utils::to_string(track),
                    records[track][TRAFFIC_ON][0]);
            }

            // Older experimental XML may have a course record but no separate
            // fastest-lap field. Derive it from stored runs when necessary.
            for (int traffic_class = 0; traffic_class < TRAFFIC_CLASSES; traffic_class++)
            {
                if (fastest_laps[track][traffic_class])
                    continue;

                uint16_t best = 0;
                for (const Record& record : records[track][traffic_class])
                {
                    if (record.best_lap_counter &&
                        (!best || record.best_lap_counter < best))
                    {
                        best = record.best_lap_counter;
                    }
                }
                fastest_laps[track][traffic_class] = best;
            }
        }
    }

    void save()
    {
        xml_parser::ptree data("timetrial_scores");

        // Read first so unknown/future Time Trial fields survive this writer.
        xml_parser::read_xml(filename(), data);

        for (int track = 0; track < TRACK_COUNT; track++)
        {
            for (int traffic_class = 0; traffic_class < TRAFFIC_CLASSES; traffic_class++)
            {
                const std::string base = class_tag(track, traffic_class);
                data.put_int(
                    base + ".fastest_lap",
                    fastest_laps[track][traffic_class]);

                for (int pos = 0; pos < TABLE_ENTRIES; pos++)
                {
                    write_record(
                        data,
                        base + ".entry" + Utils::to_string(pos),
                        records[track][traffic_class][pos]);
                }
            }

            // Compatibility aliases: old CannonBall Time Trial readers see the
            // Traffic ON fastest lap and its #1 three-lap record.
            data.put_int(
                "time_trial.score" + Utils::to_string(track),
                fastest_laps[track][TRAFFIC_ON]);
            write_record(
                data,
                "time_trial.record" + Utils::to_string(track),
                records[track][TRAFFIC_ON][0]);
        }

        xml_parser::write_xml(filename(), data);
    }

    static void format_counter(uint16_t counter, char* dst, size_t size)
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

    static void draw_time(uint16_t x, uint16_t y, const char* text, uint16_t col)
    {
        uint32_t dst = ohud.translate(x, y);

        while (*text)
        {
            uint16_t tile = static_cast<uint8_t>(*text++);

            if (tile == '\'')
                tile = 0x5E;
            else if (tile == '"')
                tile = 0x5F;
            else if (tile == '.')
                tile = 0x5B;

            video.write_text16(&dst, (col << 8) | tile);
        }
    }

    static void draw_centered(uint16_t y, const char* text, uint16_t colour)
    {
        const int length = static_cast<int>(std::string(text).size());
        int x = 20 - (length / 2);
        if (x < 0)
            x = 0;

        ohud.blit_text_new(0, y, "                                        ", colour);
        ohud.blit_text_new(static_cast<uint16_t>(x), y, text, colour);
    }

    void render_results()
    {
        ohud.blit_text_big(1, "TIME TRIAL RESULTS");
        draw_centered(5, track_name(current_track), OHud::GREEN);
        ohud.blit_text_new(
            current_traffic_class == TRAFFIC_ON ? 30 : 29,
            5,
            traffic_name(current_traffic_class),
            OHud::GREEN);

        char total_text[16];
        char best_text[16];
        format_counter(pending.total_counter, total_text, sizeof(total_text));
        format_counter(pending.best_lap_counter, best_text, sizeof(best_text));

        ohud.blit_text_new(8, 9, "TOTAL TIME", OHud::GREY);
        draw_time(23, 9, total_text, OHud::GREEN);
        ohud.blit_text_new(8, 11, "BEST LAP", OHud::GREY);
        draw_time(23, 11, best_text, OHud::GREEN);

        ohud.blit_text_new(8, 14, "OVERTAKES", OHud::GREY);
        ohud.blit_text_new(28, 14,
            Utils::to_string(static_cast<int>(pending.overtakes)).c_str(),
            OHud::GREEN);
        ohud.blit_text_new(8, 16, "VEHICLE COLLISIONS", OHud::GREY);
        ohud.blit_text_new(28, 16,
            Utils::to_string(static_cast<int>(pending.vehicle_cols)).c_str(),
            OHud::GREEN);
        ohud.blit_text_new(8, 18, "CRASHES", OHud::GREY);
        ohud.blit_text_new(28, 18,
            Utils::to_string(static_cast<int>(pending.crashes)).c_str(),
            OHud::GREEN);

        if (new_course_record)
        {
            ohud.blit_text_big(20, "NEW COURSE RECORD");
        }
        else if (qualifying_score)
        {
            const std::string text =
                "HIGH SCORE " + Utils::to_string(score_pos + 1);
            ohud.blit_text_big(20, text.c_str());
        }
        else if (outrun.ttrial.laps != RECORD_LAPS)
        {
            draw_centered(21, "3 LAPS REQUIRED FOR RECORD", OHud::GREY);
        }
    }

    void render_records()
    {
        const uint16_t X_POS   = 0;
        const uint16_t X_NAME  = 3;
        const uint16_t X_TOTAL = 8;
        const uint16_t X_BEST  = 17;
        const uint16_t X_OVT   = 26;
        const uint16_t X_COL   = 31;
        const uint16_t X_CR    = 36;

        ohud.blit_text_new(10, 0, "TIME TRIAL RECORDS", OHud::GREEN);
        draw_centered(1, track_name(current_track), OHud::GREEN);
        ohud.blit_text_new(
            current_traffic_class == TRAFFIC_ON ? 30 : 29,
            1,
            traffic_name(current_traffic_class),
            OHud::GREEN);

        ohud.blit_text_new(X_POS,   3, "#", OHud::GREY);
        ohud.blit_text_new(X_NAME,  3, "NAME", OHud::GREY);
        ohud.blit_text_new(X_TOTAL, 3, "TOTAL", OHud::GREY);
        ohud.blit_text_new(X_BEST,  3, "BEST", OHud::GREY);
        ohud.blit_text_new(X_OVT,   3, "OVT", OHud::GREY);
        ohud.blit_text_new(X_COL,   3, "COL", OHud::GREY);
        ohud.blit_text_new(X_CR,    3, "CR", OHud::GREY);

        if (current_track < 0 || current_track >= TRACK_COUNT)
            return;

        const ScoreTable& table = records[current_track][current_traffic_class];

        for (int pos = 0; pos < TABLE_ENTRIES; pos++)
        {
            const uint16_t y = static_cast<uint16_t>(5 + pos);
            const Record& record = table[pos];
            const uint16_t colour =
                qualifying_score && pos == score_pos ? OHud::GREEN : OHud::GREY;

            ohud.blit_text_new(
                0,
                y,
                "                                        ",
                OHud::GREY);

            ohud.blit_text_new(
                X_POS,
                y,
                Utils::to_string(pos + 1).c_str(),
                colour);

            if (!record.total_counter)
            {
                ohud.blit_text_new(X_NAME, y, "---", colour);
                draw_time(X_TOTAL, y, "--'--\"--", colour);
                draw_time(X_BEST, y, "--'--\"--", colour);
                ohud.blit_text_new(X_OVT, y, "-", colour);
                ohud.blit_text_new(X_COL, y, "-", colour);
                ohud.blit_text_new(X_CR, y, "-", colour);
                continue;
            }

            char initials[4] =
            {
                record.initial1 == ' ' ? '-' : record.initial1,
                record.initial2 == ' ' ? '-' : record.initial2,
                record.initial3 == ' ' ? '-' : record.initial3,
                0
            };
            ohud.blit_text_new(X_NAME, y, initials, colour);

            char total_text[16];
            char best_text[16];
            format_counter(record.total_counter, total_text, sizeof(total_text));
            format_counter(record.best_lap_counter, best_text, sizeof(best_text));
            draw_time(X_TOTAL, y, total_text, colour);
            draw_time(X_BEST, y, best_text, colour);

            ohud.blit_text_new(
                X_OVT,
                y,
                Utils::to_string(static_cast<int>(record.overtakes)).c_str(),
                colour);
            ohud.blit_text_new(
                X_COL,
                y,
                Utils::to_string(static_cast<int>(record.vehicle_cols)).c_str(),
                colour);
            ohud.blit_text_new(
                X_CR,
                y,
                Utils::to_string(static_cast<int>(record.crashes)).c_str(),
                colour);
        }

        if (qualifying_score && !initials_done)
        {
            ohud.blit_text_new(13, 25, "ENTER INITIALS", OHud::GREEN);
            ohud.blit_text_new(6, 26, "ABCDEFGHIJKLMNOPQRSTUVWXYZ.", OHud::GREY);

            const char selected[2] =
            {
                static_cast<char>(letter_selected < 26 ?
                    ('A' + letter_selected) : '.'),
                0
            };
            ohud.blit_text_new(6 + letter_selected, 26, selected, OHud::GREEN);
            ohud.blit_text_new(7, 27, "STEER LETTER  ACCEL SELECT", OHud::GREY);
        }
        else if (qualifying_score)
        {
            ohud.blit_text_new(0, 25, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 26, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 27, "                                        ", OHud::GREY);

            if (new_course_record)
                ohud.blit_text_new(11, 26, "NEW COURSE RECORD", OHud::GREEN);
            else
            {
                const std::string text =
                    "HIGH SCORE " + Utils::to_string(score_pos + 1);
                draw_centered(26, text.c_str(), OHud::GREEN);
            }
        }
    }

    void update_letter_selection()
    {
        int direction = 0;

        if (input.has_pressed(Input::LEFT))
            direction = -1;
        else if (input.has_pressed(Input::RIGHT))
            direction = 1;
        else
        {
            const int steering =
                (oinputs.input_steering & 0xFF) - 0x80;

            if (steering <= -0x30)
                direction = -1;
            else if (steering >= 0x30)
                direction = 1;

            if (direction)
            {
                if (steering_repeat > 0)
                {
                    --steering_repeat;
                    direction = 0;
                }
                else
                {
                    steering_repeat = 5;
                }
            }
            else
            {
                steering_repeat = 0;
            }
        }

        if (!direction)
            return;

        letter_selected += direction;
        if (letter_selected < 0)
            letter_selected = 26;
        else if (letter_selected > 26)
            letter_selected = 0;
    }

    void accept_letter()
    {
        if (!qualifying_score ||
            current_track < 0 || current_track >= TRACK_COUNT ||
            score_pos < 0 || score_pos >= TABLE_ENTRIES ||
            initial_selected >= 3)
        {
            return;
        }

        const char letter =
            static_cast<char>(letter_selected < 26 ?
                ('A' + letter_selected) : '.');

        Record& record =
            records[current_track][current_traffic_class][score_pos];

        if (initial_selected == 0)
            record.initial1 = letter;
        else if (initial_selected == 1)
            record.initial2 = letter;
        else
            record.initial3 = letter;

        ++initial_selected;
        save();

        if (initial_selected >= 3)
        {
            initials_done = true;
            ostats.frame_counter = ostats.frame_reset;
            ostats.time_counter = 3;
        }
    }
};

extern TimeTrialRecords time_trial_records;