/***************************************************************************
    CannonBall DX Time Trial Course Records.

    Time Trial keeps the existing per-track best-lap value for the course
    selector, but adds a proper three-lap course record for each of the 15
    tracks. Records store initials, total three-lap time, the best lap from
    that record run, overtakes, vehicle collisions and crashes.
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
    // through these two helpers: physical START is suppressed and a synthetic
    // press is produced after five seconds instead.
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

        if (course_record)
        {
            ostats.time_counter = config.engine.hiscore_timer;
            save(); // Preserve the record even if initials entry times out.
        }
        else
        {
            // Enough time to read all fifteen course records before returning
            // automatically to attract mode.
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

        if (!course_record || initials_done)
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
        // Trial returned to the menu. We now save directly from this flow, so
        // clear it to prevent a later menu visit from rewriting the XML file.
        outrun.ttrial.new_high_score = false;

        results_active = false;
        results_ticks = 0;
        record_screen_active = false;
        run_captured = false;
        course_record = false;
        current_track = -1;
        initials_done = false;
        initial_selected = 0;
    }

    bool is_record_screen_active() const
    {
        return record_screen_active;
    }

private:
    static const int RESULTS_TICKS = 30 * 5;

    std::array<Record, TRACK_COUNT> records{};
    Record pending{};

    bool results_active = false;
    int results_ticks = 0;
    bool record_screen_active = false;
    bool run_captured = false;
    bool course_record = false;
    int current_track = -1;

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

    std::string filename() const
    {
        return config.engine.jap ?
            config.data.file_ttrial_jap : config.data.file_ttrial;
    }

    void begin_results()
    {
        capture_run();
        results_active = true;
        results_ticks = 0;
        video.clear_text_ram();
    }

    void capture_run()
    {
        if (run_captured)
            return;

        load();

        current_track = track_from_level(outrun.ttrial.level);
        pending = Record{};

        const int laps = std::min<int>(outrun.ttrial.laps, 5);
        uint32_t total = 0;
        uint16_t best = 0;

        for (int lap = 0; lap < laps; lap++)
        {
            const int counter_signed = ostats.stage_counters[lap];
            if (counter_signed <= 0)
                continue;

            const uint16_t counter =
                static_cast<uint16_t>(counter_signed);
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

        course_record = false;

        if (current_track >= 0)
        {
            // Preserve the existing absolute best-lap record used by the track
            // selector, independently from the new three-lap course record.
            if (best && best < config.ttrial.best_times[current_track])
                config.ttrial.best_times[current_track] = best;

            if (laps == RECORD_LAPS && pending.total_counter)
            {
                const Record& old = records[current_track];
                course_record =
                    old.total_counter == 0 ||
                    pending.total_counter < old.total_counter;

                if (course_record)
                    records[current_track] = pending;
            }

            // Save both legacy best-lap values and the new record data now.
            // This also handles a new best lap on a run that did not beat the
            // three-lap course record.
            save();
        }

        // Menu::init() must not perform the old best-lap-only save later.
        outrun.ttrial.new_high_score = false;
        run_captured = true;
    }

    void load()
    {
        for (auto& record : records)
            record = Record{};

        xml_parser::ptree data("timetrial_scores");
        if (!xml_parser::read_xml(filename(), data))
            return;

        for (int i = 0; i < TRACK_COUNT; i++)
        {
            Record& record = records[i];
            const std::string tag =
                "time_trial.record" + Utils::to_string(i);

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
    }

    void save()
    {
        xml_parser::ptree data("timetrial_scores");

        // Read the current file first so this remains compatible with the old
        // best-lap format and with any future extra Time Trial fields.
        xml_parser::read_xml(filename(), data);

        for (int i = 0; i < TRACK_COUNT; i++)
        {
            data.put_int(
                "time_trial.score" + Utils::to_string(i),
                config.ttrial.best_times[i]);

            const Record& record = records[i];
            const std::string tag =
                "time_trial.record" + Utils::to_string(i);

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

        if (course_record)
            ohud.blit_text_big(20, "NEW COURSE RECORD");
        else if (outrun.ttrial.laps != RECORD_LAPS)
            draw_centered(21, "3 LAPS REQUIRED FOR RECORD", OHud::GREY);

        int seconds_left =
            (RESULTS_TICKS - results_ticks + 29) / 30;
        if (seconds_left < 1)
            seconds_left = 1;

        const std::string countdown =
            "RECORDS IN " + Utils::to_string(seconds_left);
        draw_centered(24, countdown.c_str(), OHud::GREEN);
    }

    void render_records()
    {
        // Match the fixed-column approach used by the Endless table. Long
        // course names and changing digit counts can never move another field.
        const uint16_t X_COURSE = 1;
        const uint16_t X_NAME = 17;
        const uint16_t X_TIME = 22;
        const uint16_t X_OVT = 32;
        const uint16_t X_CR = 37;

        ohud.blit_text_new(10, 1, "TIME TRIAL RECORDS", OHud::GREEN);
        ohud.blit_text_new(X_COURSE, 3, "COURSE", OHud::GREY);
        ohud.blit_text_new(X_NAME, 3, "NAME", OHud::GREY);
        ohud.blit_text_new(X_TIME + 2, 3, "TIME", OHud::GREY);
        ohud.blit_text_new(X_OVT, 3, "OVT", OHud::GREY);
        ohud.blit_text_new(X_CR, 3, "CR", OHud::GREY);

        for (int i = 0; i < TRACK_COUNT; i++)
        {
            const uint16_t y = static_cast<uint16_t>(5 + i);
            const Record& record = records[i];
            const uint16_t colour =
                i == current_track ? OHud::GREEN : OHud::GREY;

            // Clear the entire 40-column row first, exactly like the Endless
            // table. This prevents stale or longer previous values from making
            // the columns appear to drift.
            ohud.blit_text_new(
                0,
                y,
                "                                        ",
                OHud::GREY);

            ohud.blit_text_new(X_COURSE, y, track_name(i), colour);

            if (!record.total_counter)
            {
                ohud.blit_text_new(X_NAME, y, "---", colour);
                draw_time(X_TIME, y, "--'--\"--", colour);
                ohud.blit_text_new(X_OVT, y, "-", colour);
                ohud.blit_text_new(X_CR, y, "-", colour);
                continue;
            }

            char initials[4] =
            {
                record.initial1,
                record.initial2,
                record.initial3,
                0
            };
            ohud.blit_text_new(X_NAME, y, initials, colour);

            char time_text[16];
            format_counter(record.total_counter, time_text, sizeof(time_text));
            draw_time(X_TIME, y, time_text, colour);

            ohud.blit_text_new(
                X_OVT,
                y,
                Utils::to_string(static_cast<int>(record.overtakes)).c_str(),
                colour);
            ohud.blit_text_new(
                X_CR,
                y,
                Utils::to_string(static_cast<int>(record.crashes)).c_str(),
                colour);
        }

        if (course_record && !initials_done)
        {
            ohud.blit_text_new(13, 21, "ENTER INITIALS", OHud::GREEN);
            ohud.blit_text_new(6, 23, "ABCDEFGHIJKLMNOPQRSTUVWXYZ.", OHud::GREY);

            const char selected[2] =
            {
                static_cast<char>(letter_selected < 26 ?
                    ('A' + letter_selected) : '.'),
                0
            };
            ohud.blit_text_new(6 + letter_selected, 23, selected, OHud::GREEN);
            ohud.blit_text_new(7, 25, "STEER LETTER  ACCEL SELECT", OHud::GREY);
        }
        else if (course_record)
        {
            ohud.blit_text_new(0, 21, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 23, "                                        ", OHud::GREY);
            ohud.blit_text_new(0, 25, "                                        ", OHud::GREY);
            ohud.blit_text_new(12, 22, "NEW COURSE RECORD", OHud::GREEN);
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
        if (!course_record || current_track < 0 || initial_selected >= 3)
            return;

        const char letter =
            static_cast<char>(letter_selected < 26 ?
                ('A' + letter_selected) : '.');

        Record& record = records[current_track];
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