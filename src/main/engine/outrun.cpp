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
#include "engine/multiplayer_traffic.hpp"
#include <iostream>

namespace
{
    const int TIME_TRIAL_RESULTS_TICKS = 30 * 7;
    int time_trial_results_ticks = 0;

    bool time_trial_results_input(Input::presses press)
    {
        time_trial_records.synthetic_input(press);
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

    // Only the call sites inside the preserved Outrun implementation are
    // redirected. Player 1 and all single-player runs still execute OTraffic's
    // original code verbatim. Player 2 switches to the authoritative network
    // snapshot once one has arrived, while init/disable paths remain untouched.
    class MultiplayerTrafficProxy
    {
    public:
        void tick()
        {
            if (!multiplayer_traffic::render_remote())
            {
                ::otraffic.tick();
                multiplayer_traffic::capture_after_local_tick();
            }
        }

        void disable_traffic() { ::otraffic.disable_traffic(); }
        void init_stage1_traffic() { ::otraffic.init_stage1_traffic(); }
        void init() { ::otraffic.init(); }
    };

    MultiplayerTrafficProxy multiplayer_traffic_proxy;
}

// Only while GS_GAMEOVER is executing, make the preserved MODE_CONT branch see
// Endless as false. It therefore calls init_best_outrunners() instead of the
// prototype's direct GS_REINIT shortcut. Everywhere else this macro resolves
// to the real member value, so all Endless gameplay behaviour remains intact.
#define endless_mode ((game_state == GS_GAMEOVER) ? false : this->endless_mode)

// The preserved Time Trial exit checks input.is_pressed(Input::START). Keep
// every normal is_pressed() call unchanged, except on the Time Trial GAME OVER
// screen: suppress physical START, clear the old PRESS START row, redraw the
// Results page and synthesize a press after seven seconds.
#define is_pressed(ARG) \
    is_pressed(ARG) && !time_trial_records.suppress_physical_input(ARG) || \
    (time_trial_records.suppress_physical_input(ARG) && \
     (video.clear_text_ram(), time_trial_results_input(ARG)) && \
     (time_trial_records.begin_records_transition(), game_state = GS_INIT_BEST2, true))

// The old Time Trial branch assigns STATE_INIT_MENU after its START check.
#define STATE_INIT_MENU STATE_GAME

// Redirect only references to the global traffic object that appear in
// outrun_base.cpp. The real ::otraffic object remains unchanged everywhere else
// (including OSprites::finalise_sprites traffic sound/ordering logic).
#define otraffic multiplayer_traffic_proxy

#include "outrun_base.cpp"

#undef otraffic
#undef STATE_INIT_MENU
#undef is_pressed
#undef endless_mode
