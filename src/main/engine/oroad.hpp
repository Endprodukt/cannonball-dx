/***************************************************************************
    Road Rendering & Control - CannonBall DX extensions.

    The inherited ORoad declaration is preserved in oroad_base.hpp. Expose the
    preserved tick implementation as tick_base() while retaining tick() as the
    public DX wrapper used by the rest of the engine.
***************************************************************************/

#pragma once

#include "stdint.hpp"

#define tick tick_base(); void tick
#include "oroad_base.hpp"
#undef tick
