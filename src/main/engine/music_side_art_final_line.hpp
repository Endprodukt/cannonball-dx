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

        // Final 21:9 cleanup from the latest screenshot. The remaining blue
        // pixels are one native column farther left than the previous patch:
        //   x=495, y=178      -> light dashboard grey RGB 171,171,203
        //   x=495, y=200..207 -> lower dashboard grey RGB 140,140,156
        const music_side_art_corrections::Rgb upper_target {171, 171, 203};
        const music_side_art_corrections::Rgb lower_target {140, 140, 156};
        const uint16_t upper_pixel =
            music_side_art_corrections::map_colour(upper_target);
        const uint16_t lower_pixel =
            music_side_art_corrections::map_colour(lower_target);

        // Keep the previous successful y=178 repair and extend it by one pixel.
        for (int x = 495; x <= 535; ++x)
            music_side_art_corrections::write_pixel(buffer, x, 178, upper_pixel);

        // Only this single native column is still blue on the lower strip.
        for (int y = 200; y <= 207; ++y)
            music_side_art_corrections::write_pixel(buffer, 495, y, lower_pixel);
    }
}
