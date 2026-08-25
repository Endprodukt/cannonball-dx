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
#include "engine/ohiscore.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/ologo.hpp"
#include "engine/omusic.hpp"
#include "engine/otiles.hpp"
#include "engine/otraffic.hpp"
#include "engine/ostats.hpp"
#include "frontend/menu.hpp"
#include "directx/ffeedback.hpp"

#include <SDL.h>
#include <cstring>
#include <iostream>

#define enable enable_base
#define check_start check_start_base
#define tick tick_base
#define cycle_music cycle_music_base
#define get_music_selected get_music_selected_base
#include "omusic_palette_base.cpp"
#undef get_music_selected
#undef cycle_music
#undef tick
#undef check_start
#undef enable

namespace
{
    const int CAR_COLOR_COUNT = 8;
    int last_endless_music_stage = -1;
    Uint32 music_select_deadline_ms = 0;
    bool japanese_selected = false;

    enum MusicModeSelection
    {
        SELECT_ORIGINAL = 0,
        SELECT_ORIGINAL_JP,
        SELECT_CONTINUOUS,
        SELECT_ENDLESS,
        SELECT_TIME_TRIAL,
        SELECT_COUNT,
    };

    int wrap_car_color(int color)
    {
        while (color < 0)
            color += CAR_COLOR_COUNT;
        while (color >= CAR_COLOR_COUNT)
            color -= CAR_COLOR_COUNT;
        return color;
    }

    bool music_selection_timed_out()
    {
        return music_select_deadline_ms != 0 &&
            static_cast<Sint32>(SDL_GetTicks() - music_select_deadline_ms) >= 0;
    }

    bool apply_course_variant(bool japanese)
    {
        // Music Select changes modes after Outrun::init() has already run, so
        // the normal STATE_INIT_GAME ROM-loading path cannot switch regions for
        // us. Lazily load the Japanese program ROMs here and remap the complete
        // address/course table before the new race is initialized.
        if (japanese && !roms.load_japanese_roms())
        {
            std::cerr
                << "Japanese ROMs not loaded. Falling back to Original."
                << std::endl;
            japanese = false;
        }

        config.engine.jap = japanese ? 1 : 0;
        outrun.select_course(japanese, config.engine.prototype != 0);

        // Each course variant owns a separate score file. When switching from
        // World to Japanese (or back) after boot(), the previous table is still
        // resident in memory. Reset it first so a missing target score file
        // starts from the factory defaults instead of inheriting the other
        // region's entries. An existing score file is loaded over these defaults
        // immediately afterwards by the normal Music Select start path.
        ohiscore.init_def_scores();

        return japanese;
    }

    void save_config_with_world_default()
    {
        // ORIGINAL JP is a per-run Music Select choice, not a persistent boot
        // preference. Preserve the live Japanese mapping for the race while
        // always writing World/ORIGINAL as the saved default for the next run.
        const int runtime_jap = config.engine.jap;
        config.engine.jap = 0;
        config.save();
        config.engine.jap = runtime_jap;
    }

    void draw_submode_name(const char* text)
    {
        const uint8_t y = 5;
        const uint16_t pal = 0x8AA0;
        const int length = static_cast<int>(std::strlen(text));
        const int x_start = 20 - (length / 2);

        // The preserved selector only knows the three underlying engine modes.
        // Replace those two text rows for DX sub-modes that intentionally share
        // MODE_ORIGINAL or MODE_CONT.
        for (int x = 0; x < 40; x++)
        {
            video.write_text16(ohud.translate(x, y), 0);
            video.write_text16(ohud.translate(x, y) + 0x80, 0);
        }

        uint32_t dst_addr = ohud.translate(x_start, y);
        for (int i = 0; i < length; i++)
        {
            uint16_t c = static_cast<uint8_t>(text[i]);

            if (c == ' ')
            {
                video.write_text16(&dst_addr, 0);
                video.write_text16(0x7E + dst_addr, 0);
            }
            else if (c >= 'A' && c <= 'Z')
            {
                c = ((c - 'A') * 2) + pal;
                video.write_text16(&dst_addr, c);
                video.write_text16(0x7E + dst_addr, c + 1);
            }
        }
    }
}

int OMusic::get_music_selected()
{
    // The Music Select screen no longer holds the wheel at virtual song
    // positions with a continuous ConstantForce. main.cpp only needs the real
    // selection index so it can fire a short resistance/snap pulse when the
    // selected song changes, then stop the force again.
    return music_selected;
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

    music_select_deadline_ms = 0;
    enable_base();

    // Music Select is intentionally free between songs: no directional pull
    // and no continuous centering spring. Only the short boundary-step pulses
    // in main.cpp are active while choosing a song. disable() restores the
    // normal low-speed spring when leaving the selector.
    if (config.controls.haptic && forcefeedback::is_supported())
    {
        forcefeedback::stop();
        forcefeedback::set_centering_strength(0);
    }

    // Do not use Outrun's generic countdown to own Music Select. It jumps
    // straight to GS_INIT_GAME and therefore bypasses the DX game-mode logic.
    // Instead keep that legacy counter safely away from zero and use the same
    // real-time deadline as the Time Trial course selector.
    if (!skip_music_tick)
    {
        const int selection_seconds = config.selection_timer_seconds();
        if (selection_seconds > 0)
        {
            music_select_deadline_ms =
                SDL_GetTicks() + static_cast<Uint32>(selection_seconds) * 1000U;
        }

        ostats.time_counter = 0x30;
        ostats.frame_counter = ostats.frame_reset;
    }

    // ORIGINAL JP is deliberately never sticky. If the underlying engine mode
    // is Original, every fresh selector visit begins at World/ORIGINAL and the
    // player must press VIEW1 again to opt into JP for this run.
    japanese_selected = false;

    endless_selected =
        outrun.cannonball_mode == Outrun::MODE_CONT && outrun.endless_mode;

    // A new selector visit belongs to a new run, so allow its first scheduled
    // Endless music transition to fire again.
    last_endless_music_stage = -1;
}

void OMusic::check_start()
{
    int old_color = wrap_car_color(config.engine.car_pal);
    int color_direction = 0;

    // Resolve the five logical choices while keeping only the three existing
    // CannonBall engine modes underneath. VIEW1 toggles Original <-> Original
    // JP, VIEW2 toggles Continuous <-> Endless, and VIEW cycles all five.
    int old_selection = SELECT_ORIGINAL;
    if (game_mode_selected == Outrun::MODE_TTRIAL)
        old_selection = SELECT_TIME_TRIAL;
    else if (game_mode_selected == Outrun::MODE_CONT)
        old_selection = endless_selected ? SELECT_ENDLESS : SELECT_CONTINUOUS;
    else if (japanese_selected)
        old_selection = SELECT_ORIGINAL_JP;

    int new_selection = old_selection;
    bool mode_pressed = false;

    if (input.has_pressed(Input::VIEW1))
    {
        new_selection =
            old_selection == SELECT_ORIGINAL ? SELECT_ORIGINAL_JP :
            old_selection == SELECT_ORIGINAL_JP ? SELECT_ORIGINAL :
            SELECT_ORIGINAL;
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
        new_selection = (old_selection + 1) % SELECT_COUNT;
        mode_pressed = true;
    }

    const bool start_pressed =
        ostats.credits && input.has_pressed(Input::START);
    const bool starting_endless =
        start_pressed && old_selection == SELECT_ENDLESS;
    bool starting_japanese =
        start_pressed && old_selection == SELECT_ORIGINAL_JP;

    // Endless is a survival mode, so the global Timer OFF option must never
    // freeze its countdown. Restore the normal user setting for Original and
    // Continuous, while Time Trial keeps its existing forced timer behaviour.
    if (start_pressed)
    {
        music_select_deadline_ms = 0;

        // Commit the selected region before check_start_base() saves config and
        // refreshes the score table. Non-JP modes explicitly restore World data.
        starting_japanese = apply_course_variant(starting_japanese);
        japanese_selected = starting_japanese;

        if (old_selection == SELECT_ENDLESS)
            outrun.freeze_timer = false;
        else if (old_selection == SELECT_TIME_TRIAL)
            outrun.freeze_timer = true;
        else
            outrun.freeze_timer = config.engine.freeze_timer;
    }

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

    // The preserved start path may save config while the live JP mapping is
    // active. Immediately rewrite the persistent setting as World/ORIGINAL;
    // save_config_with_world_default() restores the live JP flag afterwards.
    if (start_pressed)
        save_config_with_world_default();

    // The preserved selector knows Original / Continuous / Time Trial. Apply
    // the two DX sub-mode states after its input handling. A VIEW press and START
    // on exactly the same frame remains intentionally undefined; normal arcade
    // use selects the mode first and confirms it afterwards.
    if (mode_pressed)
    {
        switch (new_selection)
        {
            case SELECT_ORIGINAL_JP:
                game_mode_selected = Outrun::MODE_ORIGINAL;
                japanese_selected = true;
                endless_selected = false;
                break;

            case SELECT_CONTINUOUS:
                game_mode_selected = Outrun::MODE_CONT;
                japanese_selected = false;
                endless_selected = false;
                break;

            case SELECT_ENDLESS:
                game_mode_selected = Outrun::MODE_CONT;
                japanese_selected = false;
                endless_selected = true;
                break;

            case SELECT_TIME_TRIAL:
                game_mode_selected = Outrun::MODE_TTRIAL;
                japanese_selected = false;
                endless_selected = false;
                break;

            default:
                game_mode_selected = Outrun::MODE_ORIGINAL;
                japanese_selected = false;
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
        // Keep the region persistence rule intact while applying the corrected
        // eight-colour race-only value.
        if (save_after_correction)
            save_config_with_world_default();
    }
}

void OMusic::cycle_music()
{
    if (outrun.endless_mode &&
        outrun.cannonball_mode == Outrun::MODE_CONT)
    {
        const int stage = static_cast<int>(outrun.endless_stage);

        // The old Endless prototype requested a cycle every five stages. The
        // polished mode instead changes at stages 5, 9, 13... (after each four
        // completed stages), always at a checkpoint. Ignore obsolete calls and
        // suppress duplicate requests from the same transition.
        if (stage <= 0 || (stage % 4) != 0 || stage == last_endless_music_stage)
            return;

        last_endless_music_stage = stage;
    }

    cycle_music_base();
}

void OMusic::tick()
{
    // A timed Music Select must commit the highlighted game mode exactly like
    // START. The old engine timeout could not do this because it only changed
    // game_state. Handle the full DX selection here instead.
    if (!skip_music_tick &&
        outrun.game_state == GS_MUSIC &&
        music_selection_timed_out())
    {
        music_select_deadline_ms = 0;

        bool starting_japanese =
            game_mode_selected == Outrun::MODE_ORIGINAL && japanese_selected;
        starting_japanese = apply_course_variant(starting_japanese);
        japanese_selected = starting_japanese;

        save_config_with_world_default();

        if (game_mode_selected == Outrun::MODE_TTRIAL &&
            outrun.cannonball_mode != Outrun::MODE_TTRIAL)
        {
            pending_music_selected = music_selected;
            return_from_time_trial = true;
            outrun.freeze_timer = true;
            outrun.endless_mode = false;

            cannonball::audio.clear_wav();
            osoundint.queue_sound(sound::FM_RESET);
            ologo.disable();
            disable();

            ostats.time_counter = 0x30;
            ostats.frame_counter = ostats.frame_reset;

            if (menu)
            {
                menu->start_time_trial_from_music();
                return;
            }

            // Defensive fallback for builds without a frontend menu object.
            return_from_time_trial = false;
            game_mode_selected = Outrun::MODE_ORIGINAL;
        }

        outrun.cannonball_mode = game_mode_selected;
        outrun.endless_mode =
            game_mode_selected == Outrun::MODE_CONT && endless_selected;

        if (outrun.endless_mode)
        {
            outrun.freeze_timer = false;
            outrun.custom_traffic = 2;
        }
        else if (game_mode_selected == Outrun::MODE_TTRIAL)
        {
            outrun.freeze_timer = true;
        }
        else
        {
            outrun.freeze_timer = config.engine.freeze_timer;
        }

        if (game_mode_selected == Outrun::MODE_CONT && !outrun.endless_mode)
            set_continuous_traffic_from_difficulty();

        if (game_mode_selected != Outrun::MODE_TTRIAL)
            config.load_scores(game_mode_selected == Outrun::MODE_ORIGINAL);

        outrun.game_state = GS_INIT_GAME;
        ologo.disable();
        disable();

        ostats.time_counter = 0x30;
        ostats.frame_counter = ostats.frame_reset;
        return;
    }

    tick_base();

    // Neutralise the legacy Music Select countdown on every frame. The real
    // 15/30/OFF behaviour is exclusively owned by music_select_deadline_ms.
    if (outrun.game_state == GS_MUSIC)
    {
        ostats.time_counter = 0x30;
        ostats.frame_counter = ostats.frame_reset;
    }

    if (outrun.game_state == GS_MUSIC)
    {
        if (game_mode_selected == Outrun::MODE_ORIGINAL && japanese_selected)
            draw_submode_name("ORIGINAL JP");
        else if (game_mode_selected == Outrun::MODE_CONT && endless_selected)
            draw_submode_name("ENDLESS");
    }
}
