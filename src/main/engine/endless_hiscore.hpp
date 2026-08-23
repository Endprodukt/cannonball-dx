/***************************************************************************
    CannonBall DX Endless Mode High Scores.

    Endless is ranked primarily by completed stages, then total distance.
    The normal OutRun score and elapsed driving time are retained as tie
    breakers, while the cabinet-facing table shows the survival metrics that
    matter to the mode: STAGES, KM and TIME.
***************************************************************************/

#pragma once

#include <algorithm>
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
    }

    void tick_run(uint16_t speed_kph)
    {
        ++run_ticks;
        speed_tick_sum += speed_kph;
    }

    void capture_result(uint16_t completed_stages, uint32_t score)
    {
        pending.stages = completed_stages;

        // Game logic advances at 30 Hz. Summing displayed KPH and dividing by
        // 10800 yields tenths of a kilometre:
        //   KPH * (1/30 h/3600) * 10 = KPH / 10800.
        pending.distance_tenths =
            static_cast<uint32_t>(speed_tick_sum / 10800ULL);
        pending.time_ticks = run_ticks;
        pending.score = score;
        pending.initial1 = ' ';
        pending.initial2 = ' ';
        pending.initial3 = ' ';
        result_captured = true;
    }

    void init_screen()
    {
        load();

        score_pos = -1;
        new_entry = false;
        initials_done = false;
        initial_selected = 0;
        letter_selected = 0;
        steering_repeat = 0;
        accel_old = false;

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

            osoundint.queue_sound(sound::PCM_WAVE);
            osoundint.queue_sound(sound::MUSIC_LASTWAVE);
        }
        else
        {
            // Match the normal Best OutRunners behaviour for a non-record run.
            ostats.time_counter = 5;
        }

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

    int score_pos = -1;
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

            const std::string i1 = data.get_string(tag + ".initial1", ".");
            const std::string i2 = data.get_string(tag + ".initial2", ".");
            const std::string i3 = data.get_string(tag + ".initial3", ".");

            entry.initial1 = i1.empty() || i1[0] == '.' ? ' ' : i1[0];
            entry.initial2 = i2.empty() || i2[0] == '.' ? ' ' : i2[0];
            entry.initial3 = i3.empty() || i3[0] == '.' ? ' ' : i3[0];
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
                entry.initial1 == ' ' ? "." : std::string(1, entry.initial1));
            data.put_string(tag + ".initial2",
                entry.initial2 == ' ' ? "." : std::string(1, entry.initial2));
            data.put_string(tag + ".initial3",
                entry.initial3 == ' ' ? "." : std::string(1, entry.initial3));
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
            (static_cast<uint64_t>(ticks) * 100ULL) / 30ULL;
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

    void render()
    {
        ohud.blit_text_new(10, 1, "ENDLESS OUTRUNNERS", OHud::GREEN);
        ohud.blit_text_new(3, 4, "# NAME   STAGES   DISTANCE     TIME", OHud::GREY);

        for (int row = 0; row < 7; row++)
        {
            const int pos = row;
            const Entry& entry = scores[pos];
            const int y = 6 + (row * 2);

            // Keep unused rows visually quiet until the table fills up.
            if (entry.stages == 0 &&
                entry.distance_tenths == 0 &&
                entry.time_ticks == 0 &&
                entry.score == 0)
            {
                ohud.blit_text_new(2, y, "                                      ", OHud::GREY);
                continue;
            }

            char time_text[16];
            format_time(entry.time_ticks, time_text, sizeof(time_text));

            char line[64];
            std::snprintf(
                line,
                sizeof(line),
                "%d  %c%c%c   %u STAGES   %u.%u KM   %s",
                pos + 1,
                entry.initial1,
                entry.initial2,
                entry.initial3,
                static_cast<unsigned>(entry.stages),
                static_cast<unsigned>(entry.distance_tenths / 10),
                static_cast<unsigned>(entry.distance_tenths % 10),
                time_text);

            ohud.blit_text_new(
                1,
                y,
                line,
                pos == score_pos ? OHud::GREEN : OHud::GREY);
        }

        if (new_entry && !initials_done)
        {
            ohud.blit_text_new(12, 21, "ENTER INITIALS", OHud::GREEN);
            ohud.blit_text_new(6, 23, "ABCDEFGHIJKLMNOPQRSTUVWXYZ.", OHud::GREY);

            const char selected[2] =
            {
                static_cast<char>(letter_selected < 26 ?
                    ('A' + letter_selected) : '.'),
                0
            };
            ohud.blit_text_new(6 + letter_selected, 23, selected, OHud::GREEN);
            ohud.blit_text_new(7, 26, "STEER LETTER  ACCEL SELECT", OHud::GREY);
        }
        else if (new_entry)
        {
            ohud.blit_text_new(13, 23, "NEW RECORD", OHud::GREEN);
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

        if (++initial_selected >= 3)
        {
            initials_done = true;
            ostats.frame_counter = ostats.frame_reset;
            ostats.time_counter = 2;
            save();
        }
    }
};
