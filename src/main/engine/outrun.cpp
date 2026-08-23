/***************************************************************************
    OutRun Engine Entry Point - CannonBall DX Endless wrapper.

    The current engine implementation is preserved in outrun_base.cpp.
    Endless normally shares MODE_CONT, but its original prototype deliberately
    skipped the Continuous score screen after GAME OVER. For the dedicated
    Endless score table we want the normal GS_INIT_BEST2 / GS_BEST2 path,
    because that path also performs the complete engine reset before returning
    to attract mode.
***************************************************************************/

// Pre-include every dependency used by the preserved implementation before the
// temporary member-name macro below. This guarantees the macro can affect only
// Outrun method bodies, never declarations in another header.
#include "main.hpp"
#include "trackloader.hpp"
#include "../utils.hpp"
#include "engine/oattractai.hpp"
#include "engine/oanimseq.hpp"
#include "engine/obonus.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/ohiscore.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/ologo.hpp"
#include "engine/omap.hpp"
#include "engine/omusic.hpp"
#include "engine/ooutputs.hpp"
#include "engine/outrun.hpp"
#include "engine/opalette.hpp"
#include "engine/ostats.hpp"
#include "engine/otiles.hpp"
#include "engine/otraffic.hpp"
#include "engine/outils.hpp"
#include <iostream>

// Only while GS_GAMEOVER is executing, make the preserved MODE_CONT branch see
// Endless as false. It therefore calls init_best_outrunners() instead of the
// prototype's direct GS_REINIT shortcut. Everywhere else this macro resolves
// to the real member value, so all Endless gameplay behaviour remains intact.
#define endless_mode ((game_state == GS_GAMEOVER) ? false : this->endless_mode)
#include "outrun_base.cpp"
#undef endless_mode
