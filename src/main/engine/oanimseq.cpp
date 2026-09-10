/***************************************************************************
    Bug-fix option wrapper for the preserved animation-sequence source.

    The original implementation is kept byte-for-byte in
    oanimseq_bugfix_base.cpp. Only its legacy fix_bugs read is redirected to
    the dedicated ending-palette option.
***************************************************************************/

#include "frontend/config.hpp"
#include "engine/obonus.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinputs.hpp"
#include "engine/oanimseq.hpp"

#define fix_bugs fix_bugs && false || config.bugfix_ending_palette()
#include "oanimseq_bugfix_base.cpp"
#undef fix_bugs
