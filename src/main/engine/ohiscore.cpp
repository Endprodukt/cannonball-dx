/***************************************************************************
    Best Outrunners - CannonBall DX Endless wrapper.

    The original score implementation is preserved in ohiscore_base.cpp.
    Endless substitutes only the game-end init/tick calls; attract-mode score
    display and Original/Continuous score handling continue to use the exact
    preserved implementation.
***************************************************************************/

// Pre-include the preserved implementation's dependencies before temporarily
// renaming OHiScore methods. This keeps generic tokens such as init/tick away
// from unrelated declarations in those headers.
#include "main.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/ostats.hpp"
#include "engine/outils.hpp"
#include "engine/ohiscore.hpp"
#include <iostream>

#include "engine/endless_hiscore.hpp"

#define init init_base
#define tick tick_base
#define score_position score_position_base
#include "ohiscore_base.cpp"
#undef score_position
#undef tick
#undef init

EndlessHiScore endless_hiscore;

namespace
{
    bool endless_gameover_score_screen()
    {
        return outrun.endless_mode &&
               outrun.cannonball_mode == Outrun::MODE_CONT &&
               (outrun.game_state == GS_INIT_BEST2 ||
                outrun.game_state == GS_BEST2);
    }
}

void OHiScore::init()
{
    if (!endless_gameover_score_screen())
    {
        init_base();
        return;
    }

    endless_hiscore.capture_result(
        outrun.endless_stage,
        ostats.score);
    endless_hiscore.init_screen();
}

void OHiScore::tick()
{
    if (endless_gameover_score_screen())
    {
        endless_hiscore.tick_screen();
        return;
    }

    tick_base();
}

int OHiScore::score_position()
{
    // The preserved GS_BEST2 exit saves Original/Continuous tables when this
    // reports a valid entry. Endless persists its own XML table, so suppress
    // that legacy save and keep hiscores_continuous.xml untouched.
    if (endless_gameover_score_screen())
    {
        endless_hiscore.save_if_needed();

        // GS_BEST2 immediately performs the full engine reset after this call.
        // Return the runtime to the normal Original attract state first so an
        // Endless MODE_CONT flag cannot leak into the demo/audio sequence.
        outrun.endless_mode = false;
        outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
        outrun.freeze_timer = config.engine.freeze_timer;
        return -1;
    }

    return score_position_base();
}
