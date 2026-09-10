/***************************************************************************
    Bug-fix option wrapper for the preserved crash source.

    The original implementation is kept byte-for-byte in
    ocrash_bugfix_base.cpp. Only its legacy fix_bugs read is redirected to
    the dedicated crash-engine-sound option.
***************************************************************************/

#include "frontend/config.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinputs.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/outils.hpp"
#include "engine/ocrash.hpp"

#define fix_bugs fix_bugs && false || config.bugfix_crash_engine_sound()
#include "ocrash_bugfix_base.cpp"
#undef fix_bugs
