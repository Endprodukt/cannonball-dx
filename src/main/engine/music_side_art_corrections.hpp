#pragma once

#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/outrun.hpp"
#include "video.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace music_side_art_corrections
{
    inline constexpr std::array<uint8_t, 32> STANDARD_DAC = {
        0, 8, 16, 24, 31, 39, 47, 55,
        62, 70, 78, 86, 94, 102, 109, 117,
        125, 133, 140, 148, 156, 164, 171, 179,
        187, 195, 203, 211, 218, 226, 234, 242
    };

    struct Rgb
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    struct Span
    {
        uint8_t y;
        uint16_t x;
        uint8_t length;
        uint8_t colour;
    };

    inline constexpr std::array<Rgb, 8> COLOURS = {{
        {156, 156, 171},
        {109, 125, 125},
        {78, 78, 78},
        {0, 234, 0},
        {156, 156, 156},
        {234, 234, 234},
        {171, 171, 203},
        {140, 140, 156},
    }};

    // Pixel-exact replacement areas from the final edited 536x224 BMP:
    // x=68..135, y=200..207 removes the grey bar over the steering wheel.
    // x=496..535, y=177 and y=200..207 removes the remaining blue blocks.
    inline constexpr std::array<Span, 100> SPANS = {{
        {200, 68, 1, 0},
        {200, 69, 1, 1},
        {200, 70, 1, 2},
        {200, 71, 8, 3},
        {200, 79, 2, 2},
        {200, 81, 23, 4},
        {200, 104, 1, 2},
        {200, 105, 10, 4},
        {200, 115, 1, 2},
        {200, 116, 1, 4},
        {200, 117, 12, 1},
        {200, 129, 7, 2},
        {201, 68, 1, 0},
        {201, 69, 1, 1},
        {201, 70, 1, 2},
        {201, 71, 8, 3},
        {201, 79, 2, 2},
        {201, 81, 22, 4},
        {201, 103, 1, 2},
        {201, 104, 12, 4},
        {201, 116, 1, 2},
        {201, 117, 12, 1},
        {201, 129, 1, 2},
        {201, 130, 6, 3},
        {202, 68, 1, 0},
        {202, 69, 1, 1},
        {202, 70, 1, 2},
        {202, 71, 8, 3},
        {202, 79, 1, 2},
        {202, 80, 1, 1},
        {202, 81, 1, 2},
        {202, 82, 21, 4},
        {202, 103, 1, 2},
        {202, 104, 12, 4},
        {202, 116, 1, 2},
        {202, 117, 12, 1},
        {202, 129, 1, 2},
        {202, 130, 2, 3},
        {202, 132, 3, 5},
        {202, 135, 1, 3},
        {203, 68, 1, 0},
        {203, 69, 1, 1},
        {203, 70, 10, 2},
        {203, 80, 1, 1},
        {203, 81, 1, 2},
        {203, 82, 20, 4},
        {203, 102, 1, 2},
        {203, 103, 13, 4},
        {203, 116, 1, 2},
        {203, 117, 12, 1},
        {203, 129, 1, 2},
        {203, 130, 2, 3},
        {203, 132, 1, 5},
        {203, 133, 3, 3},
        {204, 68, 1, 0},
        {204, 69, 12, 1},
        {204, 81, 1, 2},
        {204, 82, 20, 4},
        {204, 102, 1, 2},
        {204, 103, 14, 4},
        {204, 117, 2, 2},
        {204, 119, 10, 1},
        {204, 129, 1, 2},
        {204, 130, 6, 3},
        {205, 68, 1, 0},
        {205, 69, 12, 1},
        {205, 81, 1, 2},
        {205, 82, 20, 4},
        {205, 102, 1, 2},
        {205, 103, 14, 4},
        {205, 117, 2, 2},
        {205, 119, 10, 1},
        {205, 129, 7, 2},
        {206, 68, 1, 0},
        {206, 69, 12, 1},
        {206, 81, 1, 2},
        {206, 82, 18, 4},
        {206, 100, 2, 2},
        {206, 102, 15, 4},
        {206, 117, 2, 2},
        {206, 119, 17, 1},
        {207, 68, 1, 0},
        {207, 69, 12, 1},
        {207, 81, 1, 2},
        {207, 82, 18, 4},
        {207, 100, 2, 2},
        {207, 102, 14, 4},
        {207, 116, 1, 2},
        {207, 117, 2, 4},
        {207, 119, 1, 2},
        {207, 120, 16, 1},
        {177, 496, 40, 6},
        {200, 496, 40, 7},
        {201, 496, 40, 7},
        {202, 496, 40, 7},
        {203, 496, 40, 7},
        {204, 496, 40, 7},
        {205, 496, 40, 7},
        {206, 496, 40, 7},
        {207, 496, 40, 7},
    }};

    inline Rgb palette_rgb(uint16_t palette_index)
    {
        const uint16_t raw = video.read_pal16(
            S16_PALETTE_BASE + static_cast<uint32_t>(palette_index) * 2u);

        const uint8_t r5 = static_cast<uint8_t>(
            (((raw >> 0) & 0x000Fu) << 1) | ((raw >> 12) & 1u));
        const uint8_t g5 = static_cast<uint8_t>(
            (((raw >> 4) & 0x000Fu) << 1) | ((raw >> 13) & 1u));
        const uint8_t b5 = static_cast<uint8_t>(
            (((raw >> 8) & 0x000Fu) << 1) | ((raw >> 14) & 1u));

        return {STANDARD_DAC[r5], STANDARD_DAC[g5], STANDARD_DAC[b5]};
    }

    inline int colour_distance(const Rgb& a, const Rgb& b)
    {
        const int dr = static_cast<int>(a.r) - b.r;
        const int dg = static_cast<int>(a.g) - b.g;
        const int db = static_cast<int>(a.b) - b.b;
        return dr * dr + dg * dg + db * db;
    }

    inline uint16_t map_colour(const Rgb& target)
    {
        int best_distance = std::numeric_limits<int>::max();
        uint16_t best_index = 0;

        for (uint16_t index = 0; index < 0x1000u; ++index)
        {
            const int distance = colour_distance(palette_rgb(index), target);
            if (distance < best_distance)
            {
                best_distance = distance;
                best_index = index;
                if (distance == 0)
                    break;
            }
        }

        return best_index;
    }

    inline void write_pixel(uint16_t* buffer, int x, int y, uint16_t pixel)
    {
        if (!config.video.hires)
        {
            buffer[y * config.s16_width + x] = pixel;
            return;
        }

        const int physical_x = x << 1;
        const int physical_y = y << 1;
        uint16_t* dst =
            buffer + physical_y * config.s16_width + physical_x;

        dst[0] = pixel;
        dst[1] = pixel;
        dst[config.s16_width] = pixel;
        dst[config.s16_width + 1] = pixel;
    }

    inline void render(uint16_t* buffer)
    {
        if (!buffer ||
            config.video.widescreen != 2 ||
            (outrun.game_state != GS_INIT_MUSIC &&
             outrun.game_state != GS_MUSIC))
        {
            return;
        }

        std::array<uint16_t, COLOURS.size()> palette{};
        for (std::size_t i = 0; i < COLOURS.size(); ++i)
            palette[i] = map_colour(COLOURS[i]);

        for (const Span& span : SPANS)
        {
            const uint16_t pixel = palette[span.colour];
            for (int dx = 0; dx < span.length; ++dx)
                write_pixel(buffer, span.x + dx, span.y, pixel);
        }
    }
}
