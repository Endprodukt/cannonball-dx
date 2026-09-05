/***************************************************************************
    CannonBall DX Endless Mode High Scores.

    Endless is ranked primarily by completed stages, then total distance.
    The normal OutRun score and elapsed driving time are retained as tie
    breakers, while the cabinet-facing table shows the survival metrics that
    matter to the mode: STAGES, KM and TIME.
***************************************************************************/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../utils.hpp"
#include "engine/audio/osoundint.hpp"
#include "engine/oaddresses.hpp"
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
        if (!run_active)
            begin_run();

        ++run_ticks;
        speed_tick_sum += speed_kph;
    }

    void capture_result(uint16_t completed_stages, uint32_t score)
    {
        pending.stages = completed_stages;
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

        // A held pedal must be released before it can confirm the first letter.
        analog_accel_down = oinputs.input_acc >= 0x60;

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

            ostats.time_counter = config.engine.hiscore_timer;
            ostats.frame_counter = ostats.frame_reset;

            // Persist immediately so a timeout cannot lose the run.
            save();

            osoundint.queue_sound(sound::PCM_WAVE);
            osoundint.queue_sound(sound::MUSIC_LASTWAVE);
        }
        else
        {
            ostats.time_counter = 5;
            ostats.frame_counter = ostats.frame_reset;
        }

        // The stock Best OutRunners table uses tile RAM. Clear that page before
        // drawing the dedicated Endless text table.
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

        bool select_pressed =
            input.has_pressed(Input::ACCEL) ||
            input.has_pressed(Input::START);

        // Match the stock score-entry pedal hysteresis.
        if (oinputs.input_acc < 0x30)
        {
            analog_accel_down = false;
        }
        else if (oinputs.input_acc >= 0x60 && !analog_accel_down)
        {
            analog_accel_down = true;
            select_pressed = true;
        }

        if (select_pressed)
            accept_letter();
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
    bool analog_accel_down = false;

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

            if (tile == '\'')
                tile = 0x5E;
            else if (tile == '"')
                tile = 0x5F;
            else if (tile == '.')
                tile = 0x5B;

            video.write_text16(&dst, (col << 8) | tile);
        }
    }

    static void clear_text_row(uint16_t y)
    {
        ohud.blit_text_new(
            0,
            y,
            "                                        ",
            OHud::GREY);
    }

    void draw_original_initials_editor()
    {
        // Use the exact same two-row ROM alphabet used by Original/Continuous
        // and the Time Trial record entry instead of the generic 8x8 font.
        ohud.blit_text2(TEXT2_ALPHABET);

        // Endless only needs A-Z plus the full stop. The full stop is the first
        // special stock high-score tile immediately following the ROM alphabet.
        const uint32_t dot_adr = 0x110BF0;
        video.write_text16(dot_adr,        0x8D00);
        video.write_text16(dot_adr + 0x80, 0x8D01);

        // Highlight the selected two-row glyph exactly like stock OutRun.
        const uint16_t RED = 0x80;
        const uint32_t selected_adr =
            0x110BBC + (static_cast<uint32_t>(letter_selected) << 1);

        video.write_text8(
            selected_adr,
            (video.read_text8(selected_adr) & 1) | RED);
        video.write_text8(
            selected_adr + 0x80,
            (video.read_text8(selected_adr + 0x80) & 1) | RED);

        // Same large red countdown used by the normal Best OutRunners editor.
        const uint16_t BIG_RED_FONT = 0x8080;
        ohud.draw_timer2(ostats.time_counter, 0x1101EC, BIG_RED_FONT);
    }

    void render()
    {
        const uint16_t X_RANK = 1;
        const uint16_t X_NAME = 4;
        const uint16_t X_STAGES = 9;
        const uint16_t X_DISTANCE = 20;
        const uint16_t X_TIME = 31;

        const bool entering_initials = new_entry && !initials_done;
        const uint16_t header_y = entering_initials ? 5 : 4;
        const uint16_t first_row_y = entering_initials ? 7 : 6;

        // Match the other DX record screens with the original two-row OutRun
        // display font instead of the generic one-row text font.
        ohud.blit_text_big(1, "ENDLESS OUTRUNNERS");

        if (!entering_initials)
        {
            // Remove the large red entry timer after initials are complete.
            clear_text_row(3);
            clear_text_row(4);
        }

        ohud.blit_text_new(X_RANK, header_y, "#", OHud::GREY);
        ohud.blit_text_new(X_NAME, header_y, "NAME", OHud::GREY);
        ohud.blit_text_new(X_STAGES, header_y, "STAGES", OHud::GREY);
        ohud.blit_text_new(X_DISTANCE, header_y, "DISTANCE", OHud::GREY);
        ohud.blit_text_new(X_TIME + 2, header_y, "TIME", OHud::GREY);

        for (int row = 0; row < 7; row++)
        {
            const int pos = display_start + row;
            const Entry& entry = scores[pos];
            const uint16_t y = static_cast<uint16_t>(first_row_y + (row * 2));

            clear_text_row(y);

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

        // Own the stock initials-editor area so stale one-row text or a previous
        // frame cannot remain behind the two-row arcade alphabet.
        for (uint16_t y = 21; y <= 25; y++)
            clear_text_row(y);

        if (entering_initials)
        {
            draw_original_initials_editor();
        }
        else if (new_entry)
        {
            ohud.blit_text_big(21, "NEW RECORD");
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
