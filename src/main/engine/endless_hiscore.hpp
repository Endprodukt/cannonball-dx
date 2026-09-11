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
#include <string>

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

    void begin_run();

    void tick_run(uint16_t speed_kph);

    void capture_result(uint16_t completed_stages, uint32_t score);

    void init_screen();

    void tick_screen();

    void save_if_needed();

    bool has_new_entry() const;

private:
    static const int INITIAL_DOT = 26;
    static const int INITIAL_DELETE = 27;
    static const int INITIAL_END = 28;

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

    static bool better(const Entry& lhs, const Entry& rhs);

    std::string filename() const;

    void load();

    void save();

    static void format_time(uint32_t ticks, char* dst, std::size_t size);

    static void draw_time(uint16_t x, uint16_t y, const char* text, uint16_t col);

    static void clear_text_row(uint16_t y);

    void draw_original_initials_editor();

    void render();

    void update_letter_selection();

    void finish_initials_entry();

    void accept_letter();
};

extern EndlessHiScore endless_hiscore;
