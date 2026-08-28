#pragma once

#include "music_side_art_corrections.hpp"

namespace music_side_art_final_line
{
    inline void render(uint16_t* buffer)
    {
        if (!buffer ||
            config.video.widescreen != 2 ||
            (outrun.game_state != GS_INIT_MUSIC &&
             outrun.game_state != GS_MUSIC))
        {
            return;
        }

        // Final 21:9 cleanup from the latest 536x224 screenshot:
        // native x=496..535, y=178 should be dashboard grey RGB 171,171,203.
        const music_side_art_corrections::Rgb target {171, 171, 203};
        const uint16_t pixel = music_side_art_corrections::map_colour(target);

        for (int x = 496; x <= 535; ++x)
            music_side_art_corrections::write_pixel(buffer, x, 178, pixel);
    }
}
