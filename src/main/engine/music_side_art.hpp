#pragma once

// Windows headers may define max as a macro. The original renderer uses
// std::numeric_limits<int>::max(), so remove the macro before parsing it.
#ifdef max
#undef max
#endif

// Keep the proven side renderer untouched under a private namespace, then
// append only the two pixel-exact correction regions from the user's BMP.
#define music_side_art music_side_art_original
#include "music_side_art_base.hpp"
#undef music_side_art

#include "music_side_art_corrections.hpp"

namespace music_side_art
{
    inline void render(uint16_t* buffer)
    {
        music_side_art_original::render(buffer);
        music_side_art_corrections::render(buffer);
    }
}
