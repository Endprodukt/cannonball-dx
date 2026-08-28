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
        {195, 195, 203},
        {234, 234, 242},
        {242, 242, 242},
        {140, 156, 171},
        {148, 164, 179},
    }};

    // Pixel-exact corrections sampled from the user's final 536x224 BMP.
    // These are intentionally separate from the proven 68px side renderer.
    inline constexpr std::array<Span, 100> SPANS = {{
        {177, 496, 40, 7},
        {200, 68, 68, 0},
        {201, 68, 68, 0},
        {202, 68, 68, 0},
        {203, 68, 68, 0},
        {204, 68, 68, 0},
        {205, 68, 68, 0},
        {206, 68, 68, 0},
        {207, 68, 68, 0},
        {200, 496, 40, 7},
        {201, 496, 40, 7},
        {202, 496, 40, 7},
        {203, 496, 40, 7},
        {204, 496, 40, 7},
        {205, 496, 40, 7},
        {206, 496, 40, 7},
        {207, 496, 40, 7},
        {200, 68, 1, 0},{200, 69, 1, 0},{200, 70, 1, 0},{200, 71, 1, 0},{200, 72, 1, 0},{200, 73, 1, 0},{200, 74, 1, 0},{200, 75, 1, 0},
        {201, 68, 1, 0},{201, 69, 1, 0},{201, 70, 1, 0},{201, 71, 1, 0},{201, 72, 1, 0},{201, 73, 1, 0},{201, 74, 1, 0},{201, 75, 1, 0},
        {202, 68, 1, 0},{202, 69, 1, 0},{202, 70, 1, 0},{202, 71, 1, 0},{202, 72, 1, 0},{202, 73, 1, 0},{202, 74, 1, 0},{202, 75, 1, 0},
        {203, 68, 1, 0},{203, 69, 1, 0},{203, 70, 1, 0},{203, 71, 1, 0},{203, 72, 1, 0},{203, 73, 1, 0},{203, 74, 1, 0},{203, 75, 1, 0},
        {204, 68, 1, 0},{204, 69, 1, 0},{204, 70, 1, 0},{204, 71, 1, 0},{204, 72, 1, 0},{204, 73, 1, 0},{204, 74, 1, 0},{204, 75, 1, 0},
        {205, 68, 1, 0},{205, 69, 1, 0},{205, 70, 1, 0},{205, 71, 1, 0},{205, 72, 1, 0},{205, 73, 1, 0},{205, 74, 1, 0},{205, 75, 1, 0},
        {206, 68, 1, 0},{206, 69, 1, 0},{206, 70, 1, 0},{206, 71, 1, 0},{206, 72, 1, 0},{206, 73, 1, 0},{206, 74, 1, 0},{206, 75, 1, 0},
        {207, 68, 1, 0},{207, 69, 1, 0},{207, 70, 1, 0},{207, 71, 1, 0},{207, 72, 1, 0},{207, 73, 1, 0},{207, 74, 1, 0},{207, 75, 1, 0},
        {177, 496, 1, 7},{177, 497, 1, 7},{177, 498, 1, 7},{177, 499, 1, 7},
        {200, 496, 1, 7},{201, 496, 1, 7},{202, 496, 1, 7},{203, 496, 1, 7},
        {204, 496, 1, 7},{205, 496, 1, 7},{206, 496, 1, 7},{207, 496, 1, 7},
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
        int best_distance = (std::numeric_limits<int>::max)();
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
