/***************************************************************************
    Bug-fix option wrapper for the current DX statistics implementation.

    The complete DX/Endless implementation is kept in ostats_bugfix_base.cpp.
    Its preserved legacy fix_bugs read is redirected to the dedicated
    checkpoint-lap-time option.
***************************************************************************/

#include <cstdio>
#include <cstring>
#include "frontend/config.hpp"
#include "engine/audio/osoundint.hpp"
#include "engine/ohud.hpp"
#include "engine/obonus.hpp"
#include "engine/omusic.hpp"
#include "engine/outils.hpp"
#include "engine/ostats.hpp"
#include "engine/otraffic.hpp"
#include "engine/oinitengine.hpp"
#include "engine/endless_hiscore.hpp"

#define fix_bugs fix_bugs && false || config.bugfix_checkpoint_lap_time()
#include "ostats_bugfix_base.cpp"
#undef fix_bugs
