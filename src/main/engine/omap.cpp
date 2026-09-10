/***************************************************************************
    Compatibility wrapper for the current DX course-map implementation.

    Keep the existing Course Map sea correction enabled while the old aggregate
    bug-fix switch is split into user-facing options. This avoids a 4:3 map
    regression; the sea correction can be reviewed independently later.
***************************************************************************/

#include "frontend/config.hpp"
#include "engine/oferrari.hpp"
#include "engine/omap.hpp"
#include "engine/otiles.hpp"
#include "engine/otraffic.hpp"
#include "engine/ostats.hpp"
#include "frontend/ttrial.hpp"

#define fix_bugs fix_bugs && false || true
#include "omap_bugfix_base.cpp"
#undef fix_bugs
