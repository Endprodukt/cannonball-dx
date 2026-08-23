/***************************************************************************
    CannonBall DX Endless Mode High Scores.

    Endless is ranked primarily by completed stages, then total distance.
    The normal OutRun score and elapsed driving time are retained as tie
    breakers, while the cabinet-facing table shows the survival metrics that
    matter to the mode: STAGES, KM and TIME.
***************************************************************************/

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../utils.hpp"
#include "engine/audio/osoundint.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/ostats.hpp"
#include "frontend/config.hpp"
#include "sdl2/input.hpp"

class EndlessHiScore
{
public:
    static const int NO_SCORES = 20;

    struct Entry
    {
        uint16_t stages = 0;
        uint32_t distance_tenths = 0;
        uint32_t time_ticks = 0;
        uint32_t score = 0;
        char initial1 = ' ';
        char initial2 = ' ';
        char initial3 = ' ';
    };

    void begin_run()
    {
        run_ticks = 0;
        speed_tick_sum = 0;
        result_captured = false;
        run_active = true;
    }

    void tick_run(uint16_t speed_kph)
    {
        // OStats::do_timers() is driven by the emulated vertical interrupt at
        // 60 Hz (30-fps mode calls vint twice). The first GS_INGAME sample is
        // therefore also a clean run boundary that excludes the start countdown.
        if (!run_active)
            begin_run();

        ++run_ticks;
        speed_tick_sum += speed_kph;
    }

    void capture_result(uint16_t completed_stages, uint32_t score)
    {
        pending.stages = completed_stages;

        // Sum the exact KPH value already used by the in-game speedometer.
        // At 60 samples/second, division by 21600 converts the accumulated
        // KPH samples to tenths of a kilometre:
        //   KPH * (1/60 h/3600) * 10 = KPH / 21600.
        pending.distance_tenths = static_cast<uint32_t>(
            (speed_tick_sum + 10800ULL) / 21600ULL);
        pending.time_ticks = run_ticks;
        pending.score = score;
        pending.initial1 = ' ';
        pending.initial2 = ' ';
        pending.initial3 = ' ';
        result_captured = true;
        run_active = false;
    }

    void init_screen()
    {
        load();

        score_pos = -1;
        display_start = 0;
        new_entry = false;
        initials_done = false;
        initial_selected = 0;
        letter_selected = 0;
        steering_repeat = 0;

        // A player may still have the accelerator held when GAME OVER hands
        // control to the score screen. Seed the edge detector from that live
        // state so the held pedal cannot immediately enter an unwanted 'A'.
        accel_old =
            oinputs.input_acc >= 0x60 || input.is_pressed(Input::ACCEL);

        if (result_captured)
        {
            for (int i = 0; i < NO_SCORES; i++)
            {
                if (better(pending, scores[i]))
                {
                    score_pos = i;
                    break;
                }
            }
        }

        if (score_pos >= 0)
        {
            for (int i = NO_SCORES - 1; i > score_pos; i--)
                scores[i] = scores[i - 1];

            scores[score_pos] = pending;
            new_entry = true;

            display_start = score_pos - 3;
            if (display_start < 0)
                display_start = 0;
            else if (display_start > NO_SCORES - 7)
                display_start = NO_SCORES - 7;

            // The preserved high-score state sets its own timer before calling
            // init(). Restore the configured entry time because the original
            // OHiScore routine is deliberately bypassed for Endless.
            ostats.time_counter = config.engine.hiscore_timer;
            ostats.frame_counter = ostats.frame_reset;

            // Persist the result immediately with blank initials. Subsequent
            // letter presses update the same file, so a timeout cannot lose a
            // valid Endless record.
            save();

            osoundint.queue_sound(sound::PCM_WAVE);
            osoundint.queue_sound(sound::MUSIC_LASTWAVE);
        }
        else
        {
            // Match the normal Best OutRunners behaviour for a non-record run.
            ostats.time_counter = 5;
            ostats.frame_counter = ostats.frame_reset;
        }

        // The standard table is tile-based. Clear that tile page once so the
        // dedicated text-based Endless rows are not drawn over old score data.
        uint32_t tile_addr = 0x10E000;
        for (int i = 0; i <= 0x3FF; i++)
            video.write_tile32(&tile_addr, 0x200020);

        video.clear_text_ram();
        video.enabled = true;
        render();
    }

    void tick_screen()
    {
        render();

        if (!new_entry || initials_done)
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
        if (new_entry)
            save();
    }

    bool has_new_entry() const
    {
        return new_entry;
    }

private:
    std::array<Entry, NO_SCORES> scores{};
    Entry pending{};

    uint32_t run_ticks = 0;
    uint64_t speed_tick_sum = 0;
    bool result_captured = false;
    bool run_active = false;

    int score_pos = -1;
    int display_start = 0;
    bool new_entry = false;
    bool initials_done = false;
    int initial_selected = 0;
    int letter_selected = 0;
    int steering_repeat = 0;
    bool accel_old = false;

    static bool better(const Entry& lhs, const Entry& rhs)
    {
        if (lhs.stages != rhs.stages)
            return lhs.stages > rhs.stages;

        if (lhs.distance_tenths != rhs.distance_tenths)
            return lhs.distance_tenths > rhs.distance_tenths;

        if (lhs.score != rhs.score)
            return lhs.score > rhs.score;

        if (rhs.time_ticks == 0)
            return lhs.time_ticks != 0;
        if (lhs.time_ticks == 0)
            return false;

        return lhs.time_ticks < rhs.time_ticks;
    }

    std::string filename() const
    {
        return config.data.save_path +
            (config.engine.jap ?
                "hiscores_endless_jap.xml" :
                "hiscores_endless.xml");
    }

    void load()
    {
        for (auto& entry : scores)
            entry = Entry{};

        xml_parser::ptree data("endless_scores");
        const std::string path = filename();

        if (!xml_parser::read_xml(path, data))
            return;

        for (int i = 0; i < NO_SCORES; i++)
        {
            Entry& entry = scores[i];
            const std::string tag = "score" + Utils::to_string(i);

            entry.stages = static_cast<uint16_t>(
                data.get_int(tag + ".stages", 0));
            entry.distance_tenths = static_cast<uint32_t>(
                data.get_int(tag + ".distance_tenths", 0));
            entry.time_ticks = static_cast<uint32_t>(
                data.get_int(tag + ".time_ticks", 0));
            entry.score = Utils::from_hex_string(
                data.get_string(tag + ".score", "0"));

            // Endless owns its own file format. '_' denotes an unused initial,
            // leaving '.' available as a real arcade-style initial.
            const std::string i1 = data.get_string(tag + ".initial1", "_");
            const std::string i2 = data.get_string(tag + ".initial2", "_");
            const std::string i3 = data.get_string(tag + ".initial3", "_");

            entry.initial1 = i1.empty() || i1[0] == '_' ? ' ' : i1[0];
            entry.initial2 = i2.empty() || i2[0] == '_' ? ' ' : i2[0];
            entry.initial3 = i3.empty() || i3[0] == '_' ? ' ' : i3[0];
        }
    }

    void save()
    {
        xml_parser::ptree data("endless_scores");

        for (int i = 0; i < NO_SCORES; i++)
        {
            const Entry& entry = scores[i];
            const std::string tag = "score" + Utils::to_string(i);

            data.put_int(tag + ".stages", entry.stages);
            data.put_int(tag + ".distance_tenths", entry.distance_tenths);
            data.put_int(tag + ".time_ticks", entry.time_ticks);
            data.put_string(tag + ".score", Utils::to_hex_string(entry.score));
            data.put_string(tag + ".initial1",
                entry.initial1 == ' ' ? "_" : std::string(1, entry.initial1));
            data.put_string(tag + ".initial2",
                entry.initial2 == ' ' ? "_" : std::string(1, entry.initial2));
            data.put_string(tag + ".initial3",
                entry.initial3 == ' ' ? "_" : std::string(1, entry.initial3));
        }

        xml_parser::write_xml(filename(), data);
    }

    static void format_time(uint32_t ticks, char* dst, size_t size)
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

    static void draw_time(uint16_t x, uint16_t y, const char* text, uint16_t col)
    {
        uint32_t dst = ohud.translate(x, y);

        while (*text)
        {
            uint16_t tile = static_cast<uint8_t>(*text++);

            // The System 16 text font does not use ASCII indexes for the two
            // OutRun time separators. Use the same tile numbers as the stock
            // lap/high-score renderer so they do not appear as stray symbols.
            if (tile == '\'')
                tile = 0x5E;
            else if (tile == '"')
                tile = 0x5F;
            else if (tile == '.')
                tile = 0x5B;

            video.write_text16(&dst, (col << 8) | tile);
        }
    }

    void render()
    {
        // Fixed column starts keep values aligned independently of how many
        // digits a stage/distance value happens to have.
        const uint16_t X_RANK = 1;
        const uint16_t X_NAME = 4;
        const uint16_t X_STAGES = 9;
        const uint16_t X_DISTANCE = 20;
        const uint16_t X_TIME = 31;

        ohud.blit_text_new(11, 1, "ENDLESS OUTRUNNERS", OHud::GREEN);
        ohud.blit_text_new(X_RANK, 4, "#", OHud::GREY);
        ohud.blit_text_new(X_NAME, 4, "NAME", OHud::GREY);
        ohud.blit_text_new(X_STAGES, 4, "STAGES", OHud::GREY);
        ohud.blit_text_new(X_DISTANCE, 4, "DISTANCE", OHud::GREY);
        ohud.blit_text_new(X_TIME + 2, 4, "TIME", OHud::GREY);

        for (int row = 0; row < 7; row++)
        {
            const int pos = display_start + row;
            const Entry& entry = scores[pos];
            const int y = 6 + (row * 2);

            // Always clear the visible row first. This also removes longer
            // previous values when a live initials entry changes the contents.
            ohud.blit_text_new(0, y, "                                        ", OHud::GREY);

            if (entry.stages == 0 &&
                entry.distance_tenths == 0 &&
                entry.time_ticks == 0 &&
                entry.score == 0)
            {
                continue;
            }

            const uint16_t col =
                pos == score_pos ? OHud::GREEN : OHud::GREY;

            char rank_text[4];
            std::snprintf(rank_text, sizeof(rank_text), "%d", pos + 1);
            ohud.blit_text_new(X_RANK, y, rank_text, col);

            char initials[4] =
            {
                entry.initial1,
                entry.initial2,
                entry.initial3,
                0
            };
            ohud.blit_text_new(X_NAME, y, initials, col);

            char stage_text[16];
            std::snprintf(
                stage_text,
                sizeof(stage_text),
                "%u STAGES",
                static_cast<unsigned>(entry.stages));
            ohud.blit_text_new(X_STAGES, y, stage_text, col);

            char distance_text[16];
            std::snprintf(
                distance_text,
                sizeof(distance_text),
                "%u.%u KM",
                static_cast<unsigned>(entry.distance_tenths / 10),
                static_cast<unsigned>(entry.distance_tenths % 10));
            ohud.blit_text_new(X_DISTANCE, y, distance_text, col);

            char time_text[16];
            format_time(entry.time_ticks, time_text, sizeof(time_text));
            draw_time(X_TIME, y, time_text, col);
        }

        if (new_entry && !initials_done)
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

            // Row 26 is used by the stock FREE PLAY / credits presentation.
            // Keep the Endless help one row above it so both remain readable.
            ohud.blit_text_new(7, 25, "STEER LETTER  ACCEL SELECT", OHud::GREY);
        }
        else if (new_entry)
        {
            ohud.blit_text_new(15, 23, "NEW RECORD", OHud::GREEN);
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
        if (score_pos < 0 || initial_selected >= 3)
            return;

        const char letter =
            static_cast<char>(letter_selected < 26 ?
                ('A' + letter_selected) : '.');

        Entry& entry = scores[score_pos];
        if (initial_selected == 0)
            entry.initial1 = letter;
        else if (initial_selected == 1)
            entry.initial2 = letter;
        else
            entry.initial3 = letter;

        ++initial_selected;
        save();

        if (initial_selected >= 3)
        {
            initials_done = true;
            ostats.frame_counter = ostats.frame_reset;
            ostats.time_counter = 2;
        }
    }
};
