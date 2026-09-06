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

namespace
{
    const int TIME_TRIAL_RESULTS_TICKS = 30 * 7;
    int time_trial_results_ticks = 0;

    bool time_trial_results_input(Input::presses press)
    {
        // Keep the existing Results renderer/capture logic, but let DX own the
        // actual transition time. The preserved helper becomes ready at five
        // seconds; intentionally ignore that readiness until seven seconds.
        time_trial_records.synthetic_input(press);

        // The old Results helper draws "RECORDS IN n" on this row. The delay is
        // automatic now, so erase that implementation detail every frame.
        ohud.blit_text_new(
            0,
            24,
            "                                        ",
            OHud::GREY);

        if (++time_trial_results_ticks < TIME_TRIAL_RESULTS_TICKS)
            return false;

        time_trial_results_ticks = 0;
        return true;
    }

    void reset_endless_start_traffic_slots()
    {
        // init_stage1_traffic() creates five hand-authored cars and leaves their
        // traffic state machine active. Merely clearing ENABLE is insufficient:
        // OTraffic::tick() still processes TRAFFIC_INIT/ENTRY/TICK slots. Reset
        // all eight traffic entries to their normal unused state instead. This
        // preserves the slot setup required by later dynamic spawning.
        for (uint8_t i = OSprites::SPRITE_TRAFF1;
             i <= OSprites::SPRITE_TRAFF8;
             ++i)
        {
            oentry* sprite = &osprites.jump_table[i];
            sprite->init(i);
            sprite->control |= OSprites::SHADOW;
            sprite->addr = outrun.adr.sprite_porsche;
        }
    }
}

// Only while GS_GAMEOVER is executing, make the preserved MODE_CONT branch see
// Endless as false. It therefore calls init_best_outrunners() instead of the
// prototype's direct GS_REINIT shortcut. Everywhere else this macro resolves
// to the real member value, so all Endless gameplay behaviour remains intact.
#define endless_mode ((game_state == GS_GAMEOVER) ? false : this->endless_mode)

// The preserved Time Trial exit checks input.is_pressed(Input::START). Keep
// every normal is_pressed() call unchanged, except on the Time Trial GAME OVER
// screen: suppress physical START, clear the old PRESS START row, redraw the
// Results page and synthesize a press after seven seconds. The visible countdown
// from the older five-second helper is removed by time_trial_results_input().
#define is_pressed(ARG) \
    is_pressed(ARG) && !time_trial_records.suppress_physical_input(ARG) || \
    (time_trial_records.suppress_physical_input(ARG) && \
     (video.clear_text_ram(), time_trial_results_input(ARG)) && \
     (time_trial_records.begin_records_transition(), game_state = GS_INIT_BEST2, true))

// The old Time Trial branch assigns STATE_INIT_MENU after its START check.
// At this point our synthetic-input hook above has already selected
// GS_INIT_BEST2, so keep the outer CannonBall state in-game. This removes the
// frontend-menu route completely rather than trying to undo it afterwards.
#define STATE_INIT_MENU STATE_GAME

// Original and normal Continuous deliberately start Coconut Beach with five
// hand-authored traffic sprites. Endless uses dynamic traffic exclusively, so
// discard those fixed cars immediately after the preserved initializer creates
// them. Unlike disable_traffic(), this resets their traffic state machine too.
#define init_stage1_traffic() \
    init_stage1_traffic(); \
    if (endless_mode) reset_endless_start_traffic_slots()

// A non-Coconut Endless opener needs the same reduced start-object range used
// by Time Trial: the lights, sign and crowd are retained while Coconut-specific
// early objects are skipped. Route only this preserved call through the generic
// level-aware helper; ordinary Coconut starts remain visually unchanged.
#define init_startline_sprites() \
    init_startline_sprites_for_level( \
        endless_mode ? endless_start_level : 0)

#include "outrun_base.cpp"

#undef init_startline_sprites
#undef init_stage1_traffic
#undef STATE_INIT_MENU
#undef is_pressed
#undef endless_mode