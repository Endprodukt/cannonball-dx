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
#include "engine/oroad.hpp"
#include "engine/ostats.hpp"
#include "sdl2/input.hpp"

namespace
{
    ExternalOutputs external_outputs;

    bool direct_view1_old = false;
    bool direct_view2_old = false;
    bool direct_view3_old = false;

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
}

void OOutputs::writeDigitalToConsole()
{
    // Preserve the original SmartyPi console output path exactly as before.
    writeDigitalToConsole_base();

    const bool view_controls_active =
        outrun.game_state >= GS_START1 &&
        outrun.game_state <= GS_INGAME;

    // The single VIEW lamp is an availability lamp: steady on from the moment
    // the car drives in until game-over begins. No blinking.
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

    const bool music_selection =
        outrun.game_state == GS_INIT_MUSIC ||
        outrun.game_state == GS_MUSIC;

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

    const auto& settings = external_output_settings();

    external_outputs.update(
        settings.network,
        settings.windows,
        settings.port,
        cannonball::state != cannonball::STATE_QUIT,
        (start_lamp_blink || start_lamp_ingame) ? 1 : 0,
        is_set(D_BRAKE_LAMP),
        view_lamp_active ? 1 : 0,
        (view_lamp_active && view == ORoad::VIEW_ORIGINAL) ? 1 : 0,
        (view_lamp_active && view == ORoad::VIEW_ELEVATED) ? 1 : 0,
        (view_lamp_active && view == ORoad::VIEW_INCAR) ? 1 : 0);
}
