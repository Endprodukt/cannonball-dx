/***************************************************************************
    OutRun Engine Entry Point - CannonBall DX wrappers.

    The current engine implementation is preserved in outrun_base.cpp.
    Endless shares MODE_CONT but uses the normal GS_INIT_BEST2 / GS_BEST2
    reset path for its dedicated score screen. Time Trial similarly redirects
    its old PRESS START -> frontend-menu exit into an automatic Results ->
    Course Records flow.
***************************************************************************/

// Pre-include every dependency used by the preserved implementation before the
// temporary macros below. This guarantees the macros can affect only Outrun
// method bodies, never declarations in another header.
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
#include "engine/time_trial_records.hpp"
#include <iostream>

// Only while GS_GAMEOVER is executing, make the preserved MODE_CONT branch see
// Endless as false. It therefore calls init_best_outrunners() instead of the
// prototype's direct GS_REINIT shortcut. Everywhere else this macro resolves
// to the real member value, so all Endless gameplay behaviour remains intact.
#define endless_mode ((game_state == GS_GAMEOVER) ? false : this->endless_mode)

// The preserved Time Trial exit checks input.is_pressed(Input::START). Keep
// every normal is_pressed() call unchanged, except on the Time Trial GAME OVER
// screen: suppress physical START, clear the legacy blinking PRESS START row,
// redraw our Results page, and synthesize a press after five seconds.
//
// The replacement deliberately begins with the original method token: the
// source contains `input.is_pressed(...)`, so a leading parenthesis here would
// incorrectly expand to `input.(...)`.
#define is_pressed(ARG) \
    is_pressed(ARG) && !time_trial_records.suppress_physical_input(ARG) || \
    (time_trial_records.suppress_physical_input(ARG) && \
     (video.clear_text_ram(), time_trial_records.synthetic_input(ARG)))

// STATE_INIT_MENU occurs only in the preserved Time Trial GAME OVER exit. The
// original `if` has no braces, so this replacement must remain one expression
// statement. Comma operators perform all three actions only when the synthetic
// START condition is true.
#define STATE_INIT_MENU \
    STATE_GAME, time_trial_records.begin_records_transition(), game_state = GS_INIT_BEST2

#include "outrun_base.cpp"

#undef STATE_INIT_MENU
#undef is_pressed
#undef endless_mode
