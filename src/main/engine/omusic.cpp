/***************************************************************************
    Music-screen Ferrari palette extension wrapper.

    The current music selector implementation is preserved in
    omusic_palette_base.cpp. This wrapper extends its shifter colour selection
    from five to eight Ferrari palettes and adds the CannonBall DX Endless
    selector state on top of Continuous.
***************************************************************************/

#include "main.hpp"
#include "engine/car_palette_state.hpp"
#include "engine/oferrari.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/ologo.hpp"
#include "engine/omusic.hpp"
#include "engine/otiles.hpp"
#include "engine/otraffic.hpp"
#include "engine/ostats.hpp"
#include "frontend/menu.hpp"
#include "directx/ffeedback.hpp"

#include <cstring>

#define enable enable_base
#define check_start check_start_base
#define tick tick_base
#include "omusic_palette_base.cpp"
#undef tick
#undef check_start
#undef enable

namespace
{
    const int CAR_COLOR_COUNT = 8;

    enum MusicModeSelection
    {
        SELECT_ORIGINAL = 0,
        SELECT_CONTINUOUS,
        SELECT_ENDLESS,
        SELECT_TIME_TRIAL,
    };

    int wrap_car_color(int color)
    {
        while (color < 0)
            color += CAR_COLOR_COUNT;
        while (color >= CAR_COLOR_COUNT)
            color -= CAR_COLOR_COUNT;
        return color;
    }

    void draw_endless_mode_name()
    {
        const char* text = "ENDLESS";
        const uint8_t y = 5;
        const uint16_t pal = 0x8AA0;
        const int length = static_cast<int>(std::strlen(text));
        const int x_start = 20 - (length / 2);

        // The preserved selector renders MODE_CONT as CONTINUOUS. Replace only
        // those two text rows when the VIEW2 sub-mode is Endless.
        for (int x = 0; x < 40; x++)
        {
            video.write_text16(ohud.translate(x, y), 0);
            video.write_text16(ohud.translate(x, y) + 0x80, 0);
        }

        uint32_t dst_addr = ohud.translate(x_start, y);
        for (int i = 0; i < length; i++)
        {
            uint16_t c = static_cast<uint8_t>(text[i]);
            c = ((c - 'A') * 2) + pal;
            video.write_text16(&dst_addr, c);
            video.write_text16(0x7E + dst_addr, c + 1);
        }
    }
}

void OMusic::enable()
{
    // A fresh Music Select always starts from the persistent attract/default
    // colour. The shifter may then choose a temporary colour for this race.
    // The second Time Trial handoff is the same race, so preserve its already
    // selected temporary colour when returning from course selection.
    if (!(return_from_time_trial &&
          outrun.cannonball_mode == Outrun::MODE_TTRIAL))
    {
        config.engine.car_pal =
            car_palette_state::get_default(config.engine.car_pal);
    }

    enable_base();

    // Endless deliberately shares MODE_CONT. Restore its second VIEW2 state
    // when returning to Music Select after an Endless run.
    endless_selected =
        outrun.cannonball_mode == Outrun::MODE_CONT && outrun.endless_mode;
}

void OMusic::check_start()
{
    int old_color = wrap_car_color(config.engine.car_pal);
    int color_direction = 0;

    // Resolve the four logical choices while keeping only the three existing
    // CannonBall engine modes underneath. VIEW cycles all four. VIEW2 toggles
    // Continuous <-> Endless when it is pressed repeatedly.
    int old_selection = SELECT_ORIGINAL;
    if (game_mode_selected == Outrun::MODE_TTRIAL)
        old_selection = SELECT_TIME_TRIAL;
    else if (game_mode_selected == Outrun::MODE_CONT)
        old_selection = endless_selected ? SELECT_ENDLESS : SELECT_CONTINUOUS;

    int new_selection = old_selection;
    bool mode_pressed = false;

    if (input.has_pressed(Input::VIEW1))
    {
        new_selection = SELECT_ORIGINAL;
        mode_pressed = true;
    }
    else if (input.has_pressed(Input::VIEW2))
    {
        new_selection =
            old_selection == SELECT_CONTINUOUS ? SELECT_ENDLESS :
            old_selection == SELECT_ENDLESS ? SELECT_CONTINUOUS :
            SELECT_CONTINUOUS;
        mode_pressed = true;
    }
    else if (input.has_pressed(Input::VIEW3))
    {
        new_selection = SELECT_TIME_TRIAL;
        mode_pressed = true;
    }
    else if (input.has_pressed(Input::VIEWPOINT))
    {
        new_selection = (old_selection + 1) & 3;
        mode_pressed = true;
    }

    const bool start_pressed =
        ostats.credits && input.has_pressed(Input::START);
    const bool starting_endless =
        start_pressed && old_selection == SELECT_ENDLESS;

    // Mirror only the direction detection from the preserved implementation.
    // check_start_base() still owns the actual colour handling, game start,
    // FFB cleanup and Time Trial handoff.
    if (config.controls.gear == config.controls.GEAR_PRESS)
    {
        const bool low_now = input.is_pressed(Input::GEAR1);
        if (menu_gear_initialized && low_now != menu_gear_state)
            color_direction = low_now ? -1 : 1;
    }
    else if (config.controls.gear == config.controls.GEAR_SEPARATE)
    {
        if (input.has_pressed(Input::GEAR1))
            color_direction = -1;
        else if (input.has_pressed(Input::GEAR2))
            color_direction = 1;
    }
    else if (config.controls.gear == config.controls.GEAR_BUTTON)
    {
        if (input.has_pressed(Input::GEAR1))
            color_direction = oinputs.gear ? 1 : -1;
    }
    else
    {
        if (input.has_pressed(Input::GEAR1))
            color_direction = -1;
        else if (input.has_pressed(Input::GEAR2))
            color_direction = 1;
    }

    const bool save_after_correction =
        color_direction != 0 &&
        ostats.credits &&
        input.has_pressed(Input::START);

    check_start_base();

    // The preserved selector knows Original / Continuous / Time Trial. Apply
    // the fourth logical state after its input handling. A VIEW press and START
    // on exactly the same frame remains intentionally undefined; normal arcade
    // use selects the mode first and confirms it afterwards.
    if (mode_pressed)
    {
        switch (new_selection)
        {
            case SELECT_CONTINUOUS:
                game_mode_selected = Outrun::MODE_CONT;
                endless_selected = false;
                break;

            case SELECT_ENDLESS:
                game_mode_selected = Outrun::MODE_CONT;
                endless_selected = true;
                break;

            case SELECT_TIME_TRIAL:
                game_mode_selected = Outrun::MODE_TTRIAL;
                endless_selected = false;
                break;

            default:
                game_mode_selected = Outrun::MODE_ORIGINAL;
                endless_selected = false;
                break;
        }

        if (new_selection != old_selection)
            osoundint.queue_sound(sound::BEEP1);
    }

    if (start_pressed)
    {
        outrun.endless_mode = starting_endless;

        // Endless always begins at the easiest traffic density. The engine
        // raises this progressively after subsequent checkpoints.
        if (starting_endless)
            outrun.custom_traffic = 2;
    }

    if (color_direction != 0)
    {
        config.engine.car_pal =
            wrap_car_color(old_color + color_direction);

        // The preserved five-colour routine may already have saved on START.
        // Config::save() now always persists the separate attract/default
        // colour, so this correction remains race-only even on the START frame.
        if (save_after_correction)
            config.save();
    }
}

void OMusic::tick()
{
    tick_base();

    if (outrun.game_state == GS_MUSIC &&
        game_mode_selected == Outrun::MODE_CONT &&
        endless_selected)
    {
        draw_endless_mode_name();
    }
}