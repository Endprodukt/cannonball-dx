/***************************************************************************
    In-Game Statistics - CannonBall DX Endless wrapper.

    The preserved OStats implementation remains in ostats_base.cpp. Endless
    adds one lightweight sample per 60 Hz vertical-interrupt timer tick before
    delegating to the original timer/lap handling.
***************************************************************************/

// Load all preserved dependencies before renaming do_timers so the temporary
// macro cannot touch declarations in another header.
#include "engine/ohud.hpp"
#include "engine/omusic.hpp"
#include "engine/outils.hpp"
#include "engine/ostats.hpp"
#include "engine/otraffic.hpp"
#include "engine/oinitengine.hpp"
#include "engine/endless_hiscore.hpp"

#define do_timers do_timers_base
#include "ostats_base.cpp"
#undef do_timers

extern EndlessHiScore endless_hiscore;

void OStats::do_timers()
{
    if (outrun.endless_mode &&
        outrun.cannonball_mode == Outrun::MODE_CONT &&
        outrun.game_state == GS_INGAME)
    {
        endless_hiscore.tick_run(
            static_cast<uint16_t>(oinitengine.car_increment >> 16));
    }

    do_timers_base();
}
