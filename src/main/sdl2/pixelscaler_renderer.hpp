#pragma once

#include "sdl2/rendersurface.hpp"
#include "sdl2/pixel_scaler_state.hpp"
#include "frontend/config.hpp"
#include "xbrz.h"
#include "hqx.h"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <vector>

// Pixel-scaler rendering path used for xBRZ/HQx. When the scaler is OFF this
// class delegates directly to the existing RenderSurface, so the established
// CannonBall-SE CRT/Blargg renderer remains untouched.
class PixelScalerRenderer final : public RenderSurface
{
public:
    PixelScalerRenderer() = default;
    ~PixelScalerRenderer() override
    {
        if (scaler_path)
            disable_scaler();
    }

    bool init(int source_width, int source_height,
              int source_scale, int requested_video_mode,
              int requested_scanlines) override
    {
        active_mode = pixel_scaler::mode.load(std::memory_order_relaxed);
        scaler_path = pixel_scaler::active(active_mode);

        if (!scaler_path)
            return RenderSurface::init(
                source_width,
                source_height,
                source_scale,
                requested_video_mode,
                requested_scanlines);

        src_width = source_width;
        src_height = source_height;
        scale = std::max(1, source_scale);
        video_mode = requested_video_mode;
        scanlines = std::max(0, requested_scanlines);
        factor = pixel_scaler::factor(active_mode);

        if (pixel_scaler::is_hqx(active_mode))
        {
            std::call_once(hqx_init_once, []()
            {
                hqxInit();
            });
        }

        if (!sdl_screen_size())
            return false;

        if (video_mode == video_settings_t::MODE_WINDOW)
        {
            scn_width = src_width * scale;
            scn_height = src_height * scale;
        }
        else
        {
            scn_width = orig_width;
            scn_height = orig_height;
        }

        window = SDL_CreateWindow(
            "Cannonball",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            scn_width,
            scn_height,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

        if (!window)
        {
            std::cerr << "Pixel scaler window creation failed: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        if (video_mode != video_settings_t::MODE_WINDOW)
        {
            if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
            {
                std::cerr << "Pixel scaler fullscreen failed: "
                          << SDL_GetError() << std::endl;
            }
            SDL_ShowCursor(SDL_DISABLE);
        }
        else
        {
            SDL_ShowCursor(SDL_ENABLE);
        }

        Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
        if (config.video.vsync)
            renderer_flags |= SDL_RENDERER_PRESENTVSYNC;

        sdl_renderer = SDL_CreateRenderer(window, -1, renderer_flags);
        if (!sdl_renderer)
        {
            // Some SDL backends do not expose an accelerated renderer. The CPU
            // scaler remains useful, so fall back to whatever renderer exists.
            sdl_renderer = SDL_CreateRenderer(window, -1, 0);
        }

        if (!sdl_renderer)
        {
            std::cerr << "Pixel scaler renderer creation failed: "
                      << SDL_GetError() << std::endl;
            disable_scaler();
            return false;
        }

        scaled_width = src_width * factor;
        scaled_height = src_height * factor;

        texture = SDL_CreateTexture(
            sdl_renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            scaled_width,
            scaled_height);

        if (!texture)
        {
            std::cerr << "Pixel scaler texture creation failed: "
                      << SDL_GetError() << std::endl;
            disable_scaler();
            return false;
        }

        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

        source_pixels.assign(
            static_cast<size_t>(src_width) * src_height,
            0xFF000000u);
        scaled_pixels.assign(
            static_cast<size_t>(scaled_width) * scaled_height,
            0xFF000000u);

        std::cout << "Pixel scaler enabled: "
                  << pixel_scaler::name(active_mode)
                  << " (" << src_width << "x" << src_height
                  << " -> " << scaled_width << "x" << scaled_height << ")"
                  << std::endl;

        return true;
    }

    void swap_buffers() override
    {
        if (!scaler_path)
            RenderSurface::swap_buffers();
    }

    void disable() override
    {
        if (!scaler_path)
        {
            RenderSurface::disable();
            return;
        }

        disable_scaler();
    }

    bool start_frame() override
    {
        if (!scaler_path)
            return RenderSurface::start_frame();
        return true;
    }

    void draw_frame(uint16_t* pixels, int fastpass) override
    {
        if (!scaler_path)
        {
            RenderSurface::draw_frame(pixels, fastpass);
            return;
        }

        // CannonBall can call draw_frame concurrently for the top and bottom
        // halves. xBRZ/HQx operate on the complete image here; worker 0 does
        // the work and worker 1 simply returns.
        if (fastpass == 1 || !pixels)
            return;

        const size_t pixel_count =
            static_cast<size_t>(src_width) * src_height;

        for (size_t i = 0; i < pixel_count; ++i)
        {
            const uint16_t palette_index =
                pixels[i] & ((S16_PALETTE_ENTRIES * 2) - 1);
            const uint16_t raw = rgb_blargg[palette_index];

            const bool shadow = (raw & 0x8000u) != 0;
            const uint32_t r5 = (raw >> 10) & 0x1Fu;
            const uint32_t g5 = (raw >> 5) & 0x1Fu;
            const uint32_t b5 = raw & 0x1Fu;

            const auto& table = shadow ? SHADOW_DAC : STANDARD_DAC;
            const uint32_t r = table[r5];
            const uint32_t g = table[g5];
            const uint32_t b = table[b5];

            source_pixels[i] =
                0xFF000000u | (r << 16) | (g << 8) | b;
        }

        if (pixel_scaler::is_xbrz(active_mode))
        {
            xbrz::scale(
                static_cast<size_t>(factor),
                source_pixels.data(),
                scaled_pixels.data(),
                src_width,
                src_height,
                xbrz::ColorFormat::ARGB);
        }
        else
        {
            switch (factor)
            {
                case 2:
                    hq2x_32(
                        source_pixels.data(),
                        scaled_pixels.data(),
                        src_width,
                        src_height);
                    break;
                case 3:
                    hq3x_32(
                        source_pixels.data(),
                        scaled_pixels.data(),
                        src_width,
                        src_height);
                    break;
                default:
                    hq4x_32(
                        source_pixels.data(),
                        scaled_pixels.data(),
                        src_width,
                        src_height);
                    break;
            }
        }

        apply_scanlines_if_enabled();
    }

    bool finalize_frame() override
    {
        handle_toggle_hotkey();

        if (config.videoRestartRequired)
            return true;

        if (!scaler_path)
            return RenderSurface::finalize_frame();

        if (!sdl_renderer || !texture || scaled_pixels.empty())
            return false;

        if (SDL_UpdateTexture(
                texture,
                nullptr,
                scaled_pixels.data(),
                scaled_width * static_cast<int>(sizeof(uint32_t))) != 0)
        {
            std::cerr << "Pixel scaler texture upload failed: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        int output_width = scn_width;
        int output_height = scn_height;
        SDL_GetRendererOutputSize(
            sdl_renderer,
            &output_width,
            &output_height);

        SDL_Rect destination{0, 0, output_width, output_height};

        if (video_mode != video_settings_t::MODE_STRETCH)
        {
            const double source_aspect =
                static_cast<double>(src_width) /
                static_cast<double>(src_height);
            const double output_aspect =
                static_cast<double>(output_width) /
                static_cast<double>(output_height);

            if (output_aspect > source_aspect)
            {
                destination.h = output_height;
                destination.w = static_cast<int>(
                    static_cast<double>(output_height) * source_aspect);
                destination.x = (output_width - destination.w) / 2;
            }
            else
            {
                destination.w = output_width;
                destination.h = static_cast<int>(
                    static_cast<double>(output_width) / source_aspect);
                destination.y = (output_height - destination.h) / 2;
            }
        }

        destination.x += config.video.x_offset;
        destination.y += config.video.y_offset;

        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, texture, nullptr, &destination);
        SDL_RenderPresent(sdl_renderer);
        return true;
    }

    bool supports_window() override
    {
        if (!scaler_path)
            return RenderSurface::supports_window();
        return true;
    }

    bool supports_vsync() override
    {
        if (!scaler_path)
            return RenderSurface::supports_vsync();
        return true;
    }

private:
    inline static std::once_flag hqx_init_once;

    // System 16 resistor-ladder output levels. These match renderbase.cpp and
    // allow the scaler to operate on the same colours as the normal renderer.
    inline static constexpr std::array<uint32_t, 32> STANDARD_DAC = {
        0, 8, 16, 24, 31, 39, 47, 55,
        62, 70, 78, 86, 94, 102, 109, 117,
        125, 133, 140, 148, 156, 164, 171, 179,
        187, 195, 203, 211, 218, 226, 234, 242
    };

    inline static constexpr std::array<uint32_t, 32> SHADOW_DAC = {
        0, 5, 10, 15, 20, 25, 30, 35,
        40, 45, 50, 55, 60, 65, 70, 75,
        80, 85, 90, 95, 100, 105, 110, 115,
        120, 126, 130, 136, 140, 146, 150, 156
    };

    void handle_toggle_hotkey()
    {
        const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
        const bool down = keyboard && keyboard[SDL_SCANCODE_F6] != 0;

        if (down && !f6_was_down)
        {
            pixel_scaler::toggle();
            std::cout << "Pixel scaler: "
                      << pixel_scaler::name(
                          pixel_scaler::mode.load(std::memory_order_relaxed))
                      << std::endl;
            config.videoRestartRequired = true;
        }

        f6_was_down = down;
    }

    void apply_scanlines_if_enabled()
    {
        if (scanlines <= 0)
            return;

        const int shift = std::clamp(scanlines, 1, 3);
        for (int y = 1; y < scaled_height; y += 2)
        {
            uint32_t* row =
                scaled_pixels.data() +
                static_cast<size_t>(y) * scaled_width;

            for (int x = 0; x < scaled_width; ++x)
            {
                const uint32_t p = row[x];
                const uint32_t a = p & 0xFF000000u;
                const uint32_t r = ((p >> 16) & 0xFFu) >> shift;
                const uint32_t g = ((p >> 8) & 0xFFu) >> shift;
                const uint32_t b = (p & 0xFFu) >> shift;
                row[x] = a | (r << 16) | (g << 8) | b;
            }
        }
    }

    void disable_scaler()
    {
        if (texture)
        {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }

        if (sdl_renderer)
        {
            SDL_DestroyRenderer(sdl_renderer);
            sdl_renderer = nullptr;
        }

        if (window)
        {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        source_pixels.clear();
        scaled_pixels.clear();
        scaler_path = false;
    }

    bool scaler_path = false;
    bool f6_was_down = false;
    int active_mode = pixel_scaler::OFF;
    int factor = 1;
    int scale = 1;
    int scaled_width = 0;
    int scaled_height = 0;

    SDL_Window* window = nullptr;
    SDL_Renderer* sdl_renderer = nullptr;
    SDL_Texture* texture = nullptr;

    std::vector<uint32_t> source_pixels;
    std::vector<uint32_t> scaled_pixels;
};
