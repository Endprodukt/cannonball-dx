/***************************************************************************
    Bug-fix option wrapper for the preserved input source.

    The original implementation is kept byte-for-byte in
    oinputs_bugfix_base.cpp. Only its legacy fix_bugs read is redirected to
    the dedicated steering-input option.
***************************************************************************/

#include <iostream>
#include "frontend/config.hpp"
#include "engine/ocrash.hpp"
#include "engine/oinputs.hpp"
#include "engine/ostats.hpp"

#define fix_bugs fix_bugs && false || config.bugfix_steering_input()
#include "oinputs_bugfix_base.cpp"
#undef fix_bugs
