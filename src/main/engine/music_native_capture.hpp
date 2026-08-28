#pragma once

#include "main.hpp"
#include "frontend/config.hpp"
#include "video.hpp"

#include <SDL.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>

namespace music_native_capture
{
    // F12 writes the raw System 16 composition before window scaling, xBRZ/HQx,
    // scanlines and GLSL/CRT processing. In hi-res mode the 2x internal buffer
    // is sampled back to the original logical pixel grid, so a 21:9 capture is
    // always exactly 536x224 pixels and can be edited pixel-for-pixel.
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

    inline bool capture_watch_installed = false;
    inline unsigned capture_serial = 0;

    inline bool save_native_framebuffer()
    {
        if (!video.pixels || config.s16_width <= 0 || config.s16_height <= 0)
            return false;

        const int step = config.video.hires ? 2 : 1;
        const int source_width = config.s16_width;
        const int output_width = source_width / step;
        const int output_height = config.s16_height / step;

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
            0,
            output_width,
            output_height,
            32,
            SDL_PIXELFORMAT_ARGB8888);

        if (!surface)
        {
            std::cerr << "Native screenshot: SDL surface creation failed: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        for (int y = 0; y < output_height; ++y)
        {
            Uint32* dst = reinterpret_cast<Uint32*>(
                static_cast<Uint8*>(surface->pixels) + y * surface->pitch);
            const int source_y = y * step;

            for (int x = 0; x < output_width; ++x)
            {
                const int source_x = x * step;
                const uint16_t pixel_index =
                    video.pixels[source_y * source_width + source_x] & 0x1FFFu;
                const bool shadow = (pixel_index & 0x1000u) != 0;
                const uint16_t palette_index = pixel_index & 0x0FFFu;
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
                dst[x] = SDL_MapRGB(
                    surface->format,
                    dac[r5],
                    dac[g5],
                    dac[b5]);
            }
        }

        char filename[128];
        std::snprintf(
            filename,
            sizeof(filename),
            "native_music_select_%dx%d_%010u_%02u.bmp",
            output_width,
            output_height,
            static_cast<unsigned>(SDL_GetTicks()),
            ++capture_serial);

        const int result = SDL_SaveBMP(surface, filename);
        SDL_FreeSurface(surface);

        if (result != 0)
        {
            std::cerr << "Native screenshot: failed to write " << filename
                      << ": " << SDL_GetError() << std::endl;
            return false;
        }

        std::cout << "Native screenshot saved: " << filename << std::endl;
        return true;
    }

    inline int SDLCALL capture_event_watch(void*, SDL_Event* event)
    {
        if (event &&
            event->type == SDL_KEYDOWN &&
            event->key.repeat == 0 &&
            event->key.keysym.sym == SDLK_F12 &&
            config.video.widescreen == 2)
        {
            save_native_framebuffer();
        }

        // Event watches observe events; returning 1 keeps normal input handling.
        return 1;
    }

    inline void install_capture_watch()
    {
        if (!capture_watch_installed)
        {
            SDL_AddEventWatch(capture_event_watch, nullptr);
            capture_watch_installed = true;
            std::cout
                << "Music Select native capture enabled: F12 saves a raw 536x224 BMP."
                << std::endl;
        }
    }
}
