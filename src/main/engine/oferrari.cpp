/***************************************************************************
    Ferrari palette extensions for CannonBall-SE.

    The original Ferrari implementation is kept verbatim in oferrari_base.cpp.
    This wrapper extends its palette list and handles live F10 colour cycling
    during normal Ferrari updates. Attract mode shares the same edge state via
    car_palette_hotkey.hpp so the key can also be handled there safely.
***************************************************************************/

// Keep the multiplayer include first on Windows so Winsock2 is loaded before
// any platform headers that may be reached through SDL/config includes.
#include "engine/multiplayer.hpp"
#include "engine/car_palette_hotkey.hpp"
#include "engine/car_palette_state.hpp"

// Pre-include the original implementation's dependencies so the temporary
// macros below only affect tokens in oferrari_base.cpp itself.
#include "engine/oanimseq.hpp"
#include "engine/oattractai.hpp"
#include "engine/obonus.hpp"
#include "engine/ocrash.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/ooutputs.hpp"
#include "engine/ostats.hpp"
#include "engine/outils.hpp"
#include "engine/oferrari.hpp"

// Extend the existing five-colour initializer without modifying the preserved
// base implementation. PAL_CYAN occurs there only in FERRARI_PALETTES[].
#define PAL_CYAN PAL_CYAN, OFerrari::PAL_BLACK, OFerrari::PAL_WHITE, OFerrari::PAL_SILVER

// Keep the original tick logic as tick_base(); the wrapper below adds only the
// live colour hotkey and then delegates to the unchanged implementation.
#define tick tick_base
#include "oferrari_base.cpp"
#undef tick
#undef PAL_CYAN

namespace
{
    bool ttrial_goal_randomized = false;
    bool multiplayer_ingame_debug_printed = false;
}

void OFerrari::cycle_car_palette()
{
    const int size = sizeof(FERRARI_PALETTES) / sizeof(FERRARI_PALETTES[0]);

    config.engine.car_pal++;
    if (config.engine.car_pal >= size)
        config.engine.car_pal = 0;

    ferrari_pal = FERRARI_PALETTES[config.engine.car_pal];
}

void OFerrari::tick()
{
    // Player 2 deliberately remains in the Attract engine while waiting for
    // Player 1 to finish the shared race setup. During that lobby state the
    // joiner owns a temporary per-race colour, so do not restore the saved
    // attract/default colour every frame.
    const bool multiplayer_lobby_colour = multiplayer.keep_lobby_color();

    if (!multiplayer_lobby_colour &&
        outrun.game_state >= GS_INIT && outrun.game_state <= GS_LOGO)
    {
        const int color =
            car_palette_state::get_default(config.engine.car_pal);
        config.engine.car_pal = color;
        ferrari_pal = car_palette_state::palette_source(color);
    }

    if (car_palette_hotkey::pressed())
    {
        cycle_car_palette();

        // F10 is also useful on Player 2's waiting screen, but that temporary
        // choice belongs only to this race. Never turn it into the persistent
        // Attract default while the multiplayer lobby owns the colour.
        if (!multiplayer_lobby_colour && outrun.game_state == GS_ATTRACT)
        {
            car_palette_state::set_default(config.engine.car_pal);
            config.save();
        }
    }

    // A normal OutRun ending ties one of five end animations to the route.
    // Time Trial repeatedly finishes the same selected course, so choose one
    // of those five animation sets at random once when its GOAL sequence starts.
    // The bonus road is already loaded at this point; only the visible character/
    // celebration animation changes, which keeps the Time Trial course intact.
    const bool ttrial_goal =
        outrun.cannonball_mode == Outrun::MODE_TTRIAL &&
        (outrun.game_state == GS_INIT_BONUS || outrun.game_state == GS_BONUS);

    if (ttrial_goal && !ttrial_goal_randomized)
    {
        oanimseq.end_seq = static_cast<uint8_t>(outils::random() % 5);
        ttrial_goal_randomized = true;
    }
    else if (!ttrial_goal)
    {
        ttrial_goal_randomized = false;
    }

    tick_base();

    // Draw lobby text after the normal Attract/Music/Ferrari logic so the
    // underlying game cannot immediately overwrite the multiplayer overlay.
    multiplayer.draw_lobby_overlay();

    // Crash-isolation build: deliberately do NOT render the peer Ferrari yet.
    // The previous crash happens exactly as gameplay becomes active, which is
    // also the first point where draw_remote_ferrari() can run. Keeping all UDP,
    // lobby, setup and synchronized-start logic active while removing only this
    // renderer tells us unambiguously whether the renderer is responsible.
    if (!multiplayer_ingame_debug_printed &&
        multiplayer.enabled() && outrun.game_state == GS_INGAME)
    {
        multiplayer_ingame_debug_printed = true;
        std::cout << "[Multiplayer] Entered GS_INGAME - remote Ferrari rendering disabled for crash isolation" << std::endl;
    }

    if (outrun.game_state == GS_ATTRACT)
        multiplayer_ingame_debug_printed = false;
}
