/***************************************************************************
    OutRun Engine Entry Point - CannonBall DX Endless wrapper.

    The current engine implementation is preserved in outrun_base.cpp.
    Endless normally shares MODE_CONT, but its original prototype deliberately
    skipped the Continuous score screen after GAME OVER. For the dedicated
    Endless score table we want the normal GS_INIT_BEST2 / GS_BEST2 path,
    because that path also performs the complete engine reset before returning
    to attract mode.
***************************************************************************/

#include "engine/outrun.hpp"

// Only while GS_GAMEOVER is executing, make the preserved MODE_CONT branch see
// Endless as false. It therefore calls init_best_outrunners() instead of the
// prototype's direct GS_REINIT shortcut. Everywhere else this macro resolves
// to the real member value, so all Endless gameplay behaviour remains intact.
#define endless_mode ((game_state == GS_GAMEOVER) ? false : this->endless_mode)
#include "outrun_base.cpp"
#undef endless_mode
