/***************************************************************************
    Compatibility wrapper for the current DX sprite implementation.

    Shadow/sprite edge wrapping is kept permanently corrected. The preserved
    source already notes that later widescreen coordinate changes mean the old
    behaviour is no longer reliably reproducible, so this is intentionally not
    exposed as a gameplay toggle.
***************************************************************************/

#include "frontend/config.hpp"
#include "../trackloader.hpp"
#include "engine/oanimseq.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/osprites.hpp"
#include "engine/otraffic.hpp"

#define fix_bugs fix_bugs && false || true
#include "osprites_bugfix_base.cpp"
#undef fix_bugs