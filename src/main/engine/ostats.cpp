/***************************************************************************
    In-Game Statistics - CannonBall DX Endless wrapper.

    The preserved OStats implementation remains in ostats_base.cpp. Endless
    adds one lightweight sample per 30 Hz game-logic tick before delegating to
    the original timer/lap handling.
***************************************************************************/

#include "engine/ostats.hpp"
#include "engine/endless_hiscore.hpp"
#include "engine/oinitengine.hpp"

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
