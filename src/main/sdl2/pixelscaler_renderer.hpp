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
#include <new>
#include <string>
#include <utility>
#include <vector>

// xBRZ/HQx rendering path. OFF delegates to the original RenderSurface.
class PixelScalerRenderer final : public RenderSurface
{
public:
    PixelScalerRenderer() = default;

    ~PixelScalerRenderer()
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

        // Keep these values in both paths. They are required for a safe
        // renderer-only switch between the stock GL renderer and the scaler.
        src_width = source_width;
        src_height = source_height;
        scale = std::max(1, source_scale);
        video_mode = requested_video_mode;
        scanlines = std::max(0, requested_scanlines);

        if (!scaler_path)
        {
            return RenderSurface::init(
                source_width,
                source_height,
                source_scale,
                requested_video_mode,
                requested_scanlines);
        }

        return init_scaler_backend();
    }

    void swap_buffers() override
    {
        // This point is reached only after the render workers completed.
        if (pixel_scaler::consume_renderer_restart_request())
        {
            const int requested_mode =
                pixel_scaler::mode.load(std::memory_order_acquire);

            // Scaler -> scaler does NOT need a new window or SDL_Renderer.
            // Replacing those for every F6 press caused needless driver churn
            // and could crash intermittently. Only the output texture/buffer
            // depends on xBRZ/HQx mode and factor.
            if (scaler_path && pixel_scaler::active(requested_mode))
            {
                if (!reconfigure_scaler_output(requested_mode))
                {
                    std::cerr
                        << "Pixel scaler reconfiguration failed; keeping "
                        << pixel_scaler::name(active_mode)
                        << std::endl;

                    // Keep state consistent with the renderer that survived.
                    pixel_scaler::set(active_mode);
                }
                return;
            }

            // OFF <-> scaler crosses renderer backends and therefore still
            // needs one full renderer-only rebuild. The emulated video state,
            // tile RAM, text RAM, road and game framebuffers remain untouched.
            const int restart_width = src_width;
            const int restart_height = src_height;
            const int restart_scale = scale;
            const int restart_video_mode = video_mode;
            const int restart_scanlines = scanlines;

            std::cout << "Pixel scaler backend switch: "
                      << pixel_scaler::name(active_mode)
                      << " -> " << pixel_scaler::name(requested_mode)
                      << std::endl;

            if (scaler_path)
                disable_scaler();
            else
                RenderSurface::disable();

            if (!init(
                    restart_width,
                    restart_height,
                    restart_scale,
                    restart_video_mode,
                    restart_scanlines))
            {
                std::cerr << "Pixel scaler renderer restart failed." << std::endl;
            }
            return;
        }

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
        if (!pixels)
            return;

        if (fastpass != 1 && notification_visible())
            draw_scaler_notification(pixels);

        if (!scaler_path)
        {
            RenderSurface::draw_frame(pixels, fastpass);
            return;
        }

        // xBRZ/HQx need the complete frame. Worker 0 owns this operation;
        // worker 1 returns instead of writing the same output concurrently.
        if (fastpass == 1)
            return;

        // Hi-Res is a 2x CannonBall rendering pass. Decimate it back to the
        // native arcade pixel grid before xBRZ/HQx. Do not average: the scaler
        // must receive hard pixel edges like the xBRZ reference tools.
        for (int y = 0; y < scaler_input_height; ++y)
        {
            const int source_y = y * input_step;
            const size_t dst_row =
                static_cast<size_t>(y) * scaler_input_width;
            const size_t src_row =
                static_cast<size_t>(source_y) * src_width;

            for (int x = 0; x < scaler_input_width; ++x)
            {
                const int source_x = x * input_step;
                const uint16_t palette_index =
                    pixels[src_row + source_x] &
                    ((S16_PALETTE_ENTRIES * 2) - 1);
                const uint16_t raw = rgb_blargg[palette_index];

                const bool shadow = (raw & 0x8000u) != 0;
                const uint32_t r5 = (raw >> 10) & 0x1Fu;
                const uint32_t g5 = (raw >> 5) & 0x1Fu;
                const uint32_t b5 = raw & 0x1Fu;

                const auto& table = shadow ? SHADOW_DAC : STANDARD_DAC;
                const uint32_t r = table[r5];
                const uint32_t g = table[g5];
                const uint32_t b = table[b5];

                source_pixels[dst_row + x] =
                    0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }

        if (pixel_scaler::is_xbrz(active_mode))
        {
            xbrz::scale(
                static_cast<size_t>(factor),
                source_pixels.data(),
                scaled_pixels.data(),
                scaler_input_width,
                scaler_input_height,
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
                        scaler_input_width,
                        scaler_input_height);
                    break;

                case 3:
                    hq3x_32(
                        source_pixels.data(),
                        scaled_pixels.data(),
                        scaler_input_width,
                        scaler_input_height);
                    break;

                default:
                    hq4x_32(
                        source_pixels.data(),
                        scaled_pixels.data(),
                        scaler_input_width,
                        scaler_input_height);
                    break;
            }
        }

        apply_scanlines_if_enabled();
    }

    bool finalize_frame() override
    {
        handle_cycle_hotkey();

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
                static_cast<double>(scaler_input_width) /
                static_cast<double>(scaler_input_height);
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

#if SDL_VERSION_ATLEAST(2, 0, 12)
        const bool final_downscale =
            destination.w < scaled_width || destination.h < scaled_height;
        SDL_SetTextureScaleMode(
            texture,
            final_downscale ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
#endif

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

    bool init_scaler_backend()
    {
        input_step = config.video.hires ? 2 : 1;
        scaler_input_width = std::max(1, src_width / input_step);
        scaler_input_height = std::max(1, src_height / input_step);

        try
        {
            source_pixels.assign(
                static_cast<size_t>(scaler_input_width) * scaler_input_height,
                0xFF000000u);
        }
        catch (const std::bad_alloc&)
        {
            std::cerr << "Pixel scaler source buffer allocation failed."
                      << std::endl;
            scaler_path = false;
            return false;
        }

        if (!sdl_screen_size())
        {
            scaler_path = false;
            return false;
        }

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
            scaler_path = false;
            return false;
        }

        if (video_mode != video_settings_t::MODE_WINDOW)
        {
            if (SDL_SetWindowFullscreen(
                    window,
                    SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
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
            sdl_renderer = SDL_CreateRenderer(window, -1, 0);

        if (!sdl_renderer)
        {
            std::cerr << "Pixel scaler renderer creation failed: "
                      << SDL_GetError() << std::endl;
            disable_scaler();
            return false;
        }

        if (!reconfigure_scaler_output(active_mode, true))
        {
            disable_scaler();
            return false;
        }

        return true;
    }

    bool reconfigure_scaler_output(int requested_mode, bool initial = false)
    {
        if (!pixel_scaler::active(requested_mode) || !sdl_renderer)
            return false;

        if (pixel_scaler::is_hqx(requested_mode))
        {
            std::call_once(hqx_init_once, []()
            {
                hqxInit();
            });
        }

        const int new_factor = pixel_scaler::factor(requested_mode);
        const int new_width = scaler_input_width * new_factor;
        const int new_height = scaler_input_height * new_factor;

        // Allocate the replacement CPU buffer before touching the live output.
        std::vector<uint32_t> new_scaled_pixels;
        try
        {
            new_scaled_pixels.assign(
                static_cast<size_t>(new_width) * new_height,
                0xFF000000u);
        }
        catch (const std::bad_alloc&)
        {
            std::cerr << "Pixel scaler output buffer allocation failed for "
                      << pixel_scaler::name(requested_mode)
                      << std::endl;
            return false;
        }

        // Create the replacement texture before destroying the old one. This
        // makes a failed mode change non-destructive.
        SDL_Texture* new_texture = SDL_CreateTexture(
            sdl_renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            new_width,
            new_height);

        if (!new_texture)
        {
            std::cerr << "Pixel scaler texture creation failed for "
                      << pixel_scaler::name(requested_mode)
                      << ": " << SDL_GetError() << std::endl;
            return false;
        }

        SDL_SetTextureBlendMode(new_texture, SDL_BLENDMODE_NONE);

        const int previous_mode = active_mode;

        if (texture)
            SDL_DestroyTexture(texture);

        texture = new_texture;
        scaled_pixels.swap(new_scaled_pixels);
        active_mode = requested_mode;
        factor = new_factor;
        scaled_width = new_width;
        scaled_height = new_height;
        scaler_path = true;

        if (initial)
        {
            std::cout << "Pixel scaler enabled: "
                      << pixel_scaler::name(active_mode)
                      << " | frame " << src_width << "x" << src_height
                      << " | scaler input " << scaler_input_width << "x"
                      << scaler_input_height
                      << " | output " << scaled_width << "x"
                      << scaled_height;

            if (input_step == 2)
                std::cout << " (Hi-Res decimated to native arcade pixels)";

            std::cout << std::endl;
        }
        else
        {
            std::cout << "Pixel scaler texture switch: "
                      << pixel_scaler::name(previous_mode)
                      << " -> " << pixel_scaler::name(active_mode)
                      << " | " << scaled_width << "x" << scaled_height
                      << " (window/renderer preserved)"
                      << std::endl;
        }

        return true;
    }

    void handle_cycle_hotkey()
    {
        const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
        const bool down = keyboard && keyboard[SDL_SCANCODE_F6] != 0;

        if (down && !f6_was_down)
        {
            const int next = pixel_scaler::cycle();
            notification_mode = next;
            notification_until_ms = SDL_GetTicks() + 1500;

            std::cout << "Pixel scaler: "
                      << pixel_scaler::name(next)
                      << std::endl;
        }

        f6_was_down = down;
    }

    bool notification_visible() const
    {
        if (notification_until_ms == 0)
            return false;

        return static_cast<Sint32>(
            notification_until_ms - SDL_GetTicks()) > 0;
    }

    static std::array<uint8_t, 7> glyph(char c)
    {
        switch (c)
        {
            case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
            case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
            case 'C': return {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F};
            case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
            case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
            case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
            case 'I': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
            case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
            case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
            case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
            case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
            case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
            case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
            case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
            case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
            case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
            case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
            case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
            case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
            case '6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
            case ':': return {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
            default:  return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        }
    }

    std::pair<uint16_t, uint16_t> notification_palette_indices() const
    {
        uint16_t darkest = 0;
        uint16_t brightest = 0;
        int darkest_luma = 1000;
        int brightest_luma = -1;

        for (int i = 0; i < S16_PALETTE_ENTRIES * 2; ++i)
        {
            const uint16_t raw = rgb_blargg[i];
            const int luma =
                static_cast<int>((raw >> 10) & 0x1F) +
                static_cast<int>((raw >> 5) & 0x1F) +
                static_cast<int>(raw & 0x1F);

            if (luma < darkest_luma)
            {
                darkest_luma = luma;
                darkest = static_cast<uint16_t>(i);
            }

            if (luma > brightest_luma)
            {
                brightest_luma = luma;
                brightest = static_cast<uint16_t>(i);
            }
        }

        return {darkest, brightest};
    }

    void draw_scaler_notification(uint16_t* pixels)
    {
        const std::string text =
            std::string("PIXEL SCALER: ") +
            pixel_scaler::name(notification_mode);

        const int ui_scale = src_height >= (S16_HEIGHT * 2) ? 2 : 1;
        const int glyph_height = 7 * ui_scale;
        const int advance = 6 * ui_scale;
        const int padding = 2 * ui_scale;
        const int text_width =
            static_cast<int>(text.size()) * advance - ui_scale;
        const int box_width = text_width + padding * 2;
        const int box_height = glyph_height + padding * 2;
        const int box_x = std::max(0, (src_width - box_width) / 2);
        const int box_y = 4 * ui_scale;

        const auto [background, foreground] =
            notification_palette_indices();

        for (int y = 0; y < box_height; ++y)
        {
            const int py = box_y + y;
            if (py < 0 || py >= src_height)
                continue;

            for (int x = 0; x < box_width; ++x)
            {
                const int px = box_x + x;
                if (px >= 0 && px < src_width)
                    pixels[py * src_width + px] = background;
            }
        }

        int cursor_x = box_x + padding;
        const int text_y = box_y + padding;

        for (char c : text)
        {
            const auto rows = glyph(c);

            for (int row = 0; row < 7; ++row)
            {
                for (int col = 0; col < 5; ++col)
                {
                    if ((rows[row] & (1u << (4 - col))) == 0)
                        continue;

                    const int x0 = cursor_x + col * ui_scale;
                    const int y0 = text_y + row * ui_scale;

                    for (int sy = 0; sy < ui_scale; ++sy)
                    {
                        const int py = y0 + sy;
                        if (py < 0 || py >= src_height)
                            continue;

                        for (int sx = 0; sx < ui_scale; ++sx)
                        {
                            const int px = x0 + sx;
                            if (px >= 0 && px < src_width)
                                pixels[py * src_width + px] = foreground;
                        }
                    }
                }
            }

            cursor_x += advance;
        }
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
        scaled_width = 0;
        scaled_height = 0;
        scaler_path = false;
    }

    bool scaler_path = false;
    bool f6_was_down = false;
    int active_mode = pixel_scaler::OFF;
    int notification_mode = pixel_scaler::OFF;
    Uint32 notification_until_ms = 0;
    int factor = 1;
    int scale = 1;
    int input_step = 1;
    int scaler_input_width = 0;
    int scaler_input_height = 0;
    int scaled_width = 0;
    int scaled_height = 0;

    SDL_Window* window = nullptr;
    SDL_Renderer* sdl_renderer = nullptr;
    SDL_Texture* texture = nullptr;

    std::vector<uint32_t> source_pixels;
    std::vector<uint32_t> scaled_pixels;
};
