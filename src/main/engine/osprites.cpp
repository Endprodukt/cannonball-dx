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
#include "hwvideo/hwroad.hpp"

// The smooth DX road moves a rendered road edge by up to a couple of native
// pixels relative to the arcade road0_h[] value used to position scenery.
// Apply the exact inverse HScroll correction only at the final hardware-sprite
// X write. World coordinates, collision, AI and low-resolution rendering remain
// untouched. Traffic/Ferrari/crash sprites are deliberately excluded for now;
// this first pass targets ordinary road-side level scenery such as palms/signs.
// Clouds are excluded because they are screen/sky scenery rather than road-bound.
#define set_x(value) set_x((value) + \
    ((config.video.hires > 0 && \
      (outrun.game_state == GS_ATTRACT || \
       (outrun.game_state >= GS_START1 && outrun.game_state <= GS_GAMEOVER)) && \
      input->jump_index < OSprites::SPRITE_ENTRIES && \
      input->function_holder != 2 && \
      input->road_priority > 0 && input->road_priority < 0x200) \
        ? hwroad.hires_scenery_x_correction(input->road_priority) \
        : 0))

#define fix_bugs fix_bugs && false || true
#include "osprites_bugfix_base.cpp"
#undef fix_bugs
#undef set_x