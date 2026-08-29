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

        void init()
        {
            ::otraffic.init();

            // Player 2's JOIN press is deliberately consumed before the normal
            // freeplay START handler can manufacture a credit. The preserved
            // GS_INIT_GAME path still decrements one, so seed that single race
            // credit here before the original initialization continues.
            if (multiplayer.player_number() == 2 && ostats.credits == 0)
                ostats.credits = 1;
        }
    };

    MultiplayerTrafficProxy multiplayer_traffic_proxy;
}

#define endless_mode ((game_state == GS_GAMEOVER) ? false : this->endless_mode)

#define is_pressed(ARG) \
    is_pressed(ARG) && !time_trial_records.suppress_physical_input(ARG) || \
    (time_trial_records.suppress_physical_input(ARG) && \
     (video.clear_text_ram(), time_trial_results_input(ARG)) && \
     (time_trial_records.begin_records_transition(), game_state = GS_INIT_BEST2, true))

#define STATE_INIT_MENU STATE_GAME

// Redirect only references to the global traffic object that appear in
// outrun_base.cpp. The real ::otraffic object remains unchanged everywhere else.
#define otraffic multiplayer_traffic_proxy

#include "outrun_base.cpp"

#undef otraffic
#undef STATE_INIT_MENU
#undef is_pressed
#undef endless_mode
