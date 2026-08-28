#pragma once

#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/outrun.hpp"
#include "video.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace music_side_art
{
    inline constexpr std::array<uint8_t, 32> STANDARD_DAC = {
        0, 8, 16, 24, 31, 39, 47, 55,
        62, 70, 78, 86, 94, 102, 109, 117,
        125, 133, 140, 148, 156, 164, 171, 179,
        187, 195, 203, 211, 218, 226, 234, 242
    };

    inline constexpr std::array<uint8_t, 32> SHADOW_DAC = {
        0, 5, 10, 15, 20, 25, 30, 35,
        40, 45, 50, 55, 60, 65, 70, 75,
        80, 85, 90, 95, 100, 105, 110, 115,
        120, 126, 130, 136, 140, 146, 150, 156
    };

    struct Span
    {
        uint8_t y;
        uint8_t x;
        uint8_t length;
        uint8_t colour;
    };

    struct State
    {
        bool attempted_load = false;
        bool loaded = false;
        bool palette_mapped = false;
        std::vector<std::array<uint8_t, 3>> colours;
        std::vector<Span> spans;
        std::vector<uint16_t> palette_indices;
    };

    inline State& get_state()
    {
        static State state;
        return state;
    }

    inline bool load()
    {
        State& state = get_state();
        if (state.attempted_load)
            return state.loaded;

        state.attempted_load = true;

        const std::string path =
            config.data.res_path + "music_select_21x9_sides.bin";
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::cerr << "Unable to load 21:9 Music Select side artwork: "
                      << path << std::endl;
            return false;
        }

        std::vector<uint8_t> data(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        if (data.size() < 8 ||
            data[0] != 'M' || data[1] != 'S' ||
            data[2] != '2' || data[3] != '1' ||
            data[4] != 1)
        {
            std::cerr << "Invalid 21:9 Music Select side artwork: "
                      << path << std::endl;
            return false;
        }

        const uint8_t colour_count = data[5];
        const uint16_t span_count =
            static_cast<uint16_t>(data[6]) |
            (static_cast<uint16_t>(data[7]) << 8);
        const std::size_t expected_size =
            8u + static_cast<std::size_t>(colour_count) * 3u +
            static_cast<std::size_t>(span_count) * 4u;

        if (colour_count == 0 || data.size() != expected_size)
        {
            std::cerr << "Invalid 21:9 Music Select side artwork size: "
                      << path << std::endl;
            return false;
        }

        std::size_t offset = 8;
        state.colours.reserve(colour_count);
        for (uint8_t i = 0; i < colour_count; ++i)
        {
            state.colours.push_back({
                data[offset + 0],
                data[offset + 1],
                data[offset + 2]
            });
            offset += 3;
        }

        state.spans.reserve(span_count);
        for (uint16_t i = 0; i < span_count; ++i)
        {
            Span span {
                data[offset + 0],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3]
            };
            offset += 4;

            if (span.y >= S16_HEIGHT ||
                span.x >= 136 ||
                span.length == 0 ||
                static_cast<int>(span.x) + span.length > 136 ||
                span.colour >= colour_count)
            {
                std::cerr << "Invalid 21:9 Music Select side artwork span: "
                          << path << std::endl;
                state.colours.clear();
                state.spans.clear();
                return false;
            }

            state.spans.push_back(span);
        }

        state.palette_indices.resize(colour_count);
        state.loaded = true;
        return true;
    }

    inline std::array<uint8_t, 3> palette_rgb(uint16_t virtual_index)
    {
        const bool shadow = (virtual_index & 0x1000u) != 0;
        const uint16_t palette_index = virtual_index & 0x0FFFu;
        const uint16_t raw = video.read_pal16(
            S16_PALETTE_BASE + static_cast<uint32_t>(palette_index) * 2u);

        const uint8_t r5 =
            static_cast<uint8_t>(((raw >> 0) & 0x000Fu) << 1) |
            static_cast<uint8_t>((raw >> 12) & 1u);
        const uint8_t g5 =
            static_cast<uint8_t>(((raw >> 4) & 0x000Fu) << 1) |
            static_cast<uint8_t>((raw >> 13) & 1u);
        const uint8_t b5 =
            static_cast<uint8_t>(((raw >> 8) & 0x000Fu) << 1) |
            static_cast<uint8_t>((raw >> 14) & 1u);

        const auto& dac = shadow ? SHADOW_DAC : STANDARD_DAC;
        return { dac[r5], dac[g5], dac[b5] };
    }

    inline void map_palette()
    {
        State& state = get_state();

        for (std::size_t colour = 0; colour < state.colours.size(); ++colour)
        {
            const auto target = state.colours[colour];
            int best_distance = std::numeric_limits<int>::max();
            uint16_t best_index = 0;

            // Search the complete normal + shadow System 16 palette space.
            // These colours came from a native framebuffer capture, so exact
            // matches are expected; nearest-colour fallback is defensive only.
            for (uint16_t virtual_index = 0; virtual_index < 0x2000u; ++virtual_index)
            {
                const auto candidate = palette_rgb(virtual_index);
                const int dr = static_cast<int>(candidate[0]) - target[0];
                const int dg = static_cast<int>(candidate[1]) - target[1];
                const int db = static_cast<int>(candidate[2]) - target[2];
                const int distance = dr * dr + dg * dg + db * db;

                if (distance < best_distance)
                {
                    best_distance = distance;
                    best_index = virtual_index;
                    if (distance == 0)
                        break;
                }
            }

            state.palette_indices[colour] = best_index;
        }

        state.palette_mapped = true;
    }

    inline void render(uint16_t* buffer)
    {
        State& state = get_state();

        if (!buffer ||
            config.video.widescreen != 2 ||
            outrun.game_state != GS_MUSIC)
        {
            // Palette contents are rebuilt between game states. Force a fresh
            // lookup the next time Music Select becomes active.
            state.palette_mapped = false;
            return;
        }

        if (!load())
            return;

        if (!state.palette_mapped)
            map_palette();

        const bool hires = config.video.hires != 0;
        constexpr int SIDE_WIDTH = 68;
        constexpr int RIGHT_START = S16_WIDTH_ULTRAWIDE - SIDE_WIDTH;

        for (const Span& span : state.spans)
        {
            const int logical_start_x =
                span.x < SIDE_WIDTH
                    ? span.x
                    : RIGHT_START + (span.x - SIDE_WIDTH);
            const uint16_t pixel = state.palette_indices[span.colour];

            for (int dx = 0; dx < span.length; ++dx)
            {
                const int logical_x = logical_start_x + dx;

                if (!hires)
                {
                    buffer[static_cast<int>(span.y) * config.s16_width + logical_x] =
                        pixel;
                }
                else
                {
                    const int physical_x = logical_x << 1;
                    const int physical_y = static_cast<int>(span.y) << 1;
                    uint16_t* dst =
                        buffer + physical_y * config.s16_width + physical_x;

                    dst[0] = pixel;
                    dst[1] = pixel;
                    dst[config.s16_width] = pixel;
                    dst[config.s16_width + 1] = pixel;
                }
            }
        }
    }
}
