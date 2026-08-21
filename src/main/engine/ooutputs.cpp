/***************************************************************************
    Process Outputs - CannonBall-SE external output extensions.

    The existing output implementation, including SmartyPi console output,
    is retained verbatim in ooutputs_base.cpp. This wrapper adds MAME-
    compatible network and Windows outputs in parallel.
***************************************************************************/

#include "engine/ooutputs.hpp"
#include "engine/external_outputs.hpp"
#include "engine/external_output_settings.hpp"

// Keep the existing SmartyPi output implementation unchanged, but retain it
// under a private name so the public method can add the external transports.
#define writeDigitalToConsole writeDigitalToConsole_base
#include "engine/ooutputs_base.cpp"
#undef writeDigitalToConsole

#include "main.hpp"
#include "engine/omusic.hpp"
#include "engine/oroad.hpp"
#include "engine/ostats.hpp"
#include "sdl2/input.hpp"

namespace
{
    ExternalOutputs external_outputs;

    bool direct_view1_old = false;
    bool direct_view2_old = false;
    bool direct_view3_old = false;
    int music_mode_synced = -1;

    void handle_direct_view_buttons(bool controls_active)
    {
        const bool view1 = input.is_pressed(Input::VIEW1);
        const bool view2 = input.is_pressed(Input::VIEW2);
        const bool view3 = input.is_pressed(Input::VIEW3);

        if (controls_active)
        {
            if (view1 && !direct_view1_old)
                oroad.set_view_mode(ORoad::VIEW_ORIGINAL);
            else if (view2 && !direct_view2_old)
                oroad.set_view_mode(ORoad::VIEW_ELEVATED);
            else if (view3 && !direct_view3_old)
                oroad.set_view_mode(ORoad::VIEW_INCAR);
        }

        direct_view1_old = view1;
        direct_view2_old = view2;
        direct_view3_old = view3;
    }

    void sync_music_selection_mode(bool music_selection)
    {
        if (!music_selection)
        {
            music_mode_synced = -1;
            return;
        }

        const int selected = omusic.get_game_mode();

        // A newly selected Time Trial still needs the course map. Keep the
        // music-selection timer alive so the normal timeout cannot start a
        // stale/unselected Time Trial track. A legacy Time Trial has already
        // chosen its course, so its normal timer behaviour is preserved.
        if (selected == Outrun::MODE_TTRIAL &&
            outrun.cannonball_mode != Outrun::MODE_TTRIAL)
        {
            ostats.time_counter = config.sound.music_timer;
            ostats.frame_counter = ostats.frame_reset;
            music_mode_synced = selected;
            return;
        }

        if (selected != Outrun::MODE_ORIGINAL &&
            selected != Outrun::MODE_CONT)
        {
            music_mode_synced = selected;
            return;
        }

        // Keep the underlying engine mode aligned with what the player sees.
        // This also makes the original Music Select auto-timeout launch the
        // selected Original/Continuous mode rather than whichever mode booted.
        outrun.cannonball_mode = selected;

        if (selected != music_mode_synced)
        {
            config.load_scores(selected == Outrun::MODE_ORIGINAL);
            music_mode_synced = selected;
        }
    }

    void sync_continuous_traffic_to_difficulty()
    {
        if (outrun.cannonball_mode != Outrun::MODE_CONT)
            return;

        // Use the exact same stage/difficulty table as original OutRun. The
        // Continuous route visits all 15 stages, but stage_lookup_off / 8 still
        // identifies the original stage group (1..5) for traffic purposes.
        static const uint8_t ORIGINAL_TRAFFIC[] =
        {
            2, 2, 3, 4, 5, // Easy
            3, 4, 5, 6, 7, // Normal
            4, 5, 6, 7, 8, // Hard
            5, 6, 7, 8, 8, // Hardest
        };

        int difficulty = config.engine.dip_traffic;
        if (difficulty < 0)
            difficulty = 0;
        else if (difficulty > 3)
            difficulty = 3;

        int stage_group = oroad.stage_lookup_off / 8;
        if (stage_group < 0)
            stage_group = 0;
        else if (stage_group > 4)
            stage_group = 4;

        outrun.custom_traffic =
            ORIGINAL_TRAFFIC[(difficulty * 5) + stage_group];
    }
}

void OOutputs::writeDigitalToConsole()
{
    const bool music_selection =
        outrun.game_state == GS_INIT_MUSIC ||
        outrun.game_state == GS_MUSIC;

    // Keep the visible Music Select choice and the underlying mode in sync,
    // including the original automatic Music Select timeout behaviour.
    sync_music_selection_mode(music_selection);

    // Keep Continuous traffic tied to the normal OutRun difficulty setting.
    // This is intentionally independent of whether external outputs are enabled;
    // this method is already called every engine tick by main.cpp.
    sync_continuous_traffic_to_difficulty();

    // Preserve the original SmartyPi console output path exactly as before.
    writeDigitalToConsole_base();

    const bool view_controls_active =
        outrun.game_state >= GS_START1 &&
        outrun.game_state <= GS_INGAME;

    // During the race the single VIEW lamp remains the normal availability
    // lamp. During music selection it becomes a mode-selection lamp and blinks
    // in sync with the one matching VIEW1/2/3 lamp below.
    const bool view_lamp_active =
        outrun.game_state >= GS_START1 &&
        outrun.game_state < GS_INIT_GAMEOVER;

    handle_direct_view_buttons(view_controls_active);

    const uint8_t view = oroad.get_view_mode();

    // MAMEHooker START lamp behaviour is deliberately cabinet-oriented rather
    // than tied to the original D_START_LAMP bit. In particular, CannonBall's
    // freeplay PRESS START text does not set the original hardware bit.
    const bool press_start_screen =
        outrun.game_state == GS_ATTRACT ||
        outrun.game_state == GS_BEST1 ||
        outrun.game_state == GS_LOGO ||
        outrun.game_state == GS_BEST2;

    const bool press_start_available =
        config.engine.freeplay || ostats.credits > 0;

    // Blink during attract PRESS START and throughout music selection. The
    // attract phase uses the same BIT_4 timing as OHud::draw_insert_coin().
    const bool start_lamp_blink =
        ((press_start_screen && press_start_available) ||
         music_selection) &&
        (outrun.tick_counter & BIT_4);

    // As soon as music selection hands off to the game, keep START steadily
    // illuminated through the driving sequence, race and bonus sequence.
    const bool start_lamp_ingame =
        outrun.game_state >= GS_INIT_GAME &&
        outrun.game_state <= GS_BONUS;

    // Music-select game mode indication. The traditional single VIEW lamp
    // always blinks here, while only the lamp for the selected direct-view
    // button blinks with it. The other two remain off.
    const bool mode_lamp_blink =
        music_selection &&
        (outrun.tick_counter & BIT_4);

    const int selected_game_mode = omusic.get_game_mode();

    const auto& settings = external_output_settings();

    external_outputs.update(
        settings.network,
        settings.windows,
        settings.port,
        cannonball::state != cannonball::STATE_QUIT,
        (start_lamp_blink || start_lamp_ingame) ? 1 : 0,
        is_set(D_BRAKE_LAMP),
        music_selection
            ? (mode_lamp_blink ? 1 : 0)
            : (view_lamp_active ? 1 : 0),
        music_selection
            ? ((mode_lamp_blink && selected_game_mode == Outrun::MODE_ORIGINAL) ? 1 : 0)
            : ((view_lamp_active && view == ORoad::VIEW_ORIGINAL) ? 1 : 0),
        music_selection
            ? ((mode_lamp_blink && selected_game_mode == Outrun::MODE_CONT) ? 1 : 0)
            : ((view_lamp_active && view == ORoad::VIEW_ELEVATED) ? 1 : 0),
        music_selection
            ? ((mode_lamp_blink && selected_game_mode == Outrun::MODE_TTRIAL) ? 1 : 0)
            : ((view_lamp_active && view == ORoad::VIEW_INCAR) ? 1 : 0));
}
