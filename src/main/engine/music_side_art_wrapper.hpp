#pragma once

// Keep the proven 21:9 side renderer byte-for-byte unchanged, but expose it
// under a private namespace so we can append two tiny pixel-exact corrections.
#define music_side_art music_side_art_original
#include "music_side_art.hpp"
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
