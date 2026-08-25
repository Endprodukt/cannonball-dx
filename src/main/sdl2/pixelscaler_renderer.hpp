#pragma once

#include "sdl2/rendersurface.hpp"
#include "sdl2/pixel_scaler_state.hpp"
#include "frontend/config.hpp"
#include "xbrz.h"
#include "hqx.h"

#include <SDL.h>
#include <SDL_opengles2.h>
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

// xBRZ/HQx are only a pre-processing stage. The SDL window, GLES context,
// shader program and the complete CannonBall-SE CRT post-processing stack stay
// alive for the whole video session. Switching the pixel scaler OFF/ON only
// changes the CPU pre-processing path and the storage/format of texGame.
class PixelScalerRenderer final : public RenderSurface
{
public:
    PixelScalerRenderer() = default;

    ~PixelScalerRenderer() override
    {
        if (base_renderer_initialized)
            disable();
    }

    bool init(int source_width, int source_height,
              int source_scale, int requested_video_mode,
              int requested_scanlines) override
    {
        const int requested_mode = pixel_scaler::normalize(
            pixel_scaler::mode.load(std::memory_order_relaxed));

        active_mode = pixel_scaler::OFF;
        scaler_path = false;
        base_renderer_initialized = false;
        scaler_last_config = -1;
        scaler_ticks = 3;

        src_width = source_width;
        src_height = source_height;
        scale = std::max(1, source_scale);
        video_mode = requested_video_mode;
        scanlines = std::max(0, requested_scanlines);

        // Always initialise the stock SE renderer first. It owns the one and
        // only SDL window / GLES context / shader program. The scaler is then
        // optionally attached to that live renderer without replacing it.
        if (!RenderSurface::init(
                source_width,
                source_height,
                source_scale,
                requested_video_mode,
                requested_scanlines))
        {
            return false;
        }

        base_renderer_initialized = true;

        if (pixel_scaler::active(requested_mode))
        {
            if (!enable_scaler_in_place(requested_mode, true))
            {
                std::cerr << "Pixel scaler initialisation failed; using OFF."
                          << std::endl;
                pixel_scaler::set(pixel_scaler::OFF);
                restore_stock_game_texture();
            }
        }

        return true;
    }

    void swap_buffers() override
    {
        if (pixel_scaler::consume_renderer_restart_request())
        {
            const int requested_mode = pixel_scaler::normalize(
                pixel_scaler::mode.load(std::memory_order_acquire));

            // Do not mutate scaler buffers or texGame while either a CPU render
            // worker or the GLES presenter is still using the previous state.
            wait_for_renderer_idle();

            if (pixel_scaler::active(requested_mode))
            {
                if (!reconfigure_scaler_in_place(requested_mode))
                {
                    std::cerr << "Pixel scaler switch failed; keeping "
                              << pixel_scaler::name(active_mode)
                              << std::endl;
                    pixel_scaler::set(active_mode);
                }
                return;
            }

            if (scaler_path)
            {
                std::cout << "Pixel scaler texture switch: "
                          << pixel_scaler::name(active_mode)
                          << " -> OFF (same GLES context)" << std::endl;
                disable_scaler_in_place();
            }

            // We are now back on stock SE CPU surfaces. Keep their normal
            // double-buffer cadence from this very frame onward.
            RenderSurface::swap_buffers();
            return;
        }

        if (!scaler_path)
            RenderSurface::swap_buffers();
    }

    void disable() override
    {
        if (!base_renderer_initialized)
            return;

        // RenderSurface::disable() waits on activity_counter. Custom scaler
        // work participates in that counter below, so this also safely waits
        // for xBRZ/HQx before deleting the GLES context and stock surfaces.
        RenderSurface::disable();
        base_renderer_initialized = false;

        std::lock_guard<std::mutex> processing_lock(scaler_processing_mutex);
        release_scaler_buffers_locked();
        scaler_path = false;
        active_mode = pixel_scaler::OFF;
    }

    bool start_frame() override
    {
        return true;
    }

    void draw_frame(uint16_t* pixels, int fastpass) override
    {
        if (!pixels || config.videoRestartRequired)
            return;

        if (fastpass != 1 && notification_visible())
            draw_scaler_notification(pixels);

        // Serialise only scaler state/buffers. The stock SE path retains its
        // own existing drawFrameMutex/double buffering when the scaler is OFF.
        std::unique_lock<std::mutex> processing_lock(scaler_processing_mutex);
        if (!scaler_path)
        {
            processing_lock.unlock();
            RenderSurface::draw_frame(pixels, fastpass);
            return;
        }

        if (fastpass == 1 || shutting_down.load(std::memory_order_acquire))
            return;

        activity_counter.fetch_add(1, std::memory_order_acq_rel);

        for (int y = 0; y < scaler_input_height; ++y)
        {
            const int source_y = y * input_step;
            const size_t dst_row = static_cast<size_t>(y) * scaler_input_width;
            const size_t src_row = static_cast<size_t>(source_y) * src_width;

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
                source_pixels[dst_row + x] =
                    0xFF000000u |
                    (table[r5] << 16) |
                    (table[g5] << 8) |
                    table[b5];
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
            if (factor == 3)
            {
                hq3x_32(
                    source_pixels.data(),
                    scaled_pixels.data(),
                    scaler_input_width,
                    scaler_input_height);
            }
            else
            {
                hq4x_32(
                    source_pixels.data(),
                    scaled_pixels.data(),
                    scaler_input_width,
                    scaler_input_height);
            }
        }

        apply_low_factor_detail_preserve();
        apply_se_scanlines_after_scaler();
        prepare_rgba_upload();

        activity_counter.fetch_sub(1, std::memory_order_acq_rel);
        processing_lock.unlock();
        std::unique_lock<std::mutex> lock(mtx);
        cv.notify_all();
    }

    bool finalize_frame() override
    {
        handle_cycle_hotkey();

        if (config.videoRestartRequired)
            return true;

        if (!base_renderer_initialized || !window || !glContext)
            return false;

        if (shutting_down.load(std::memory_order_acquire))
            return true;

        activity_counter.fetch_add(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> gpulock(gpuMutex);

        if (FrameCounter++ == 60)
            FrameCounter = 0;

        if (scaler_path)
        {
            // Only a fully converted scaler frame is ever published here.
            std::lock_guard<std::mutex> frame_lock(upload_mutex);
            if (!upload_pixels.empty())
            {
                glb::update_game_texture(
                    upload_pixels.data(),
                    scaled_width * static_cast<int>(sizeof(uint32_t)),
                    scaled_width,
                    scaled_height);
            }
        }
        else
        {
            // Exact stock SE CPU path: GameSurface already contains RGB555 or
            // Blargg output plus SE scanlines. Only presentation is shared with
            // the scaler so uniform state never goes through a second cache.
            SDL_Surface* localGameSurface = nullptr;
            {
                std::lock_guard<std::mutex> lock(drawFrameMutex);
                const int idx = current_game_surface ^ 1;
                localGameSurface = GameSurface[idx];
            }

            if (localGameSurface)
            {
                glb::update_game_texture(
                    localGameSurface->pixels,
                    localGameSurface->pitch,
                    src_rect.w,
                    src_rect.h);
            }
        }

        configure_se_shader_if_needed();
        glb::set_uniform2(
            "u_Time", float(FrameCounter) / 60.0f, 0.0f);

        const int this_crt_shape_config =
            config.video.crt_shape + config.video.warpX + config.video.warpY;

        if (((config.video.vignette != last_vignette) &&
             (config.video.shader_mode < 2)) ||
            (this_crt_shape_config != last_crt_shape_config))
        {
            init_overlay();
        }

        const int x0 = anchor_x + config.video.x_offset;
        const int y0 = anchor_y + config.video.y_offset;
        glb::set_present_rect_pixels_top_left(
            x0, y0, dst_rect.w, dst_rect.h);
        glb::set_overlay_rect_pixels_top_left(
            x0, y0, dst_rect.w, dst_rect.h);

        glb::draw(
            false,
            (config.video.crt_shape != 0) ||
            (config.video.shadow_mask == 1));
        glb::present();

        activity_counter.fetch_sub(1, std::memory_order_acq_rel);
        std::unique_lock<std::mutex> lock(mtx);
        cv.notify_all();
        return true;
    }

    bool supports_window() override
    {
        return RenderSurface::supports_window();
    }

    bool supports_vsync() override
    {
        return RenderSurface::supports_vsync();
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

    void wait_for_renderer_idle()
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]
        {
            return activity_counter.load(std::memory_order_acquire) == 0;
        });
    }

    bool enable_scaler_in_place(int requested_mode, bool initial)
    {
        if (!pixel_scaler::active(requested_mode) ||
            !base_renderer_initialized || !window || !glContext)
        {
            return false;
        }

        std::lock_guard<std::mutex> processing_lock(scaler_processing_mutex);

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
            return false;
        }

        return reconfigure_scaler_locked(requested_mode, initial);
    }

    bool reconfigure_scaler_in_place(int requested_mode)
    {
        if (!pixel_scaler::active(requested_mode))
            return false;

        if (!scaler_path)
            return enable_scaler_in_place(requested_mode, false);

        std::lock_guard<std::mutex> processing_lock(scaler_processing_mutex);
        return reconfigure_scaler_locked(requested_mode, false);
    }

    bool reconfigure_scaler_locked(int requested_mode, bool initial)
    {
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

        std::vector<uint32_t> new_scaled_pixels;
        std::vector<uint32_t> new_upload_pixels;
        std::vector<uint32_t> new_staging_upload_pixels;

        try
        {
            const size_t count = static_cast<size_t>(new_width) * new_height;
            new_scaled_pixels.assign(count, 0xFF000000u);
            new_upload_pixels.assign(count, 0x000000FFu);
            new_staging_upload_pixels.assign(count, 0x000000FFu);
        }
        catch (const std::bad_alloc&)
        {
            std::cerr << "Pixel scaler output buffer allocation failed for "
                      << pixel_scaler::name(requested_mode)
                      << std::endl;
            return false;
        }

        const int previous_mode = active_mode;

        // Serialize the state commit with presentation. finalize_frame() holds
        // gpuMutex before reading scaler dimensions / uploading texGame.
        std::lock_guard<std::mutex> gpulock(gpuMutex);
        {
            std::lock_guard<std::mutex> frame_lock(upload_mutex);
            scaled_pixels.swap(new_scaled_pixels);
            upload_pixels.swap(new_upload_pixels);
            staging_upload_pixels.swap(new_staging_upload_pixels);
        }

        active_mode = requested_mode;
        factor = new_factor;
        scaled_width = new_width;
        scaled_height = new_height;
        scaler_path = true;

        set_game_texture_storage(
            scaled_width,
            scaled_height,
            glb::State::PixFmt::RGBA);

        scaler_last_config = -1;
        scaler_ticks = 3;

        if (initial)
        {
            std::cout << "Pixel scaler + stock SE post-process enabled: "
                      << pixel_scaler::name(active_mode)
                      << " | scaler " << scaled_width << "x" << scaled_height
                      << " | SE output " << dst_rect.w << "x" << dst_rect.h
                      << std::endl;
        }
        else
        {
            std::cout << "Pixel scaler texture switch: "
                      << pixel_scaler::name(previous_mode)
                      << " -> " << pixel_scaler::name(active_mode)
                      << " | " << scaled_width << "x" << scaled_height
                      << " (same GLES context)" << std::endl;
        }

        return true;
    }

    void disable_scaler_in_place()
    {
        std::lock_guard<std::mutex> processing_lock(scaler_processing_mutex);
        std::lock_guard<std::mutex> gpulock(gpuMutex);

        scaler_path = false;
        active_mode = pixel_scaler::OFF;
        factor = 1;

        restore_stock_game_texture_locked();
        release_scaler_buffers_locked();

        scaler_last_config = -1;
        scaler_ticks = 3;
    }

    void restore_stock_game_texture()
    {
        std::lock_guard<std::mutex> gpulock(gpuMutex);
        restore_stock_game_texture_locked();
    }

    void restore_stock_game_texture_locked()
    {
        const glb::State::PixFmt fmt =
            blargg ? glb::State::PixFmt::RGBA
                   : glb::State::PixFmt::RGB555;
        set_game_texture_storage(src_rect.w, src_rect.h, fmt);
    }

    void set_game_texture_storage(
        int width,
        int height,
        glb::State::PixFmt fmt)
    {
        glb::set_game_pixel_format(fmt);
        glb::G.gameW = width;
        glb::G.gameH = height;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glb::G.texGame);

        const bool rgb555 = fmt == glb::State::PixFmt::RGB555;
        const GLenum internal_format = rgb555 ? GL_RGB5_A1 : GL_RGBA;
        const GLenum external_format = GL_RGBA;
        const GLenum type = rgb555
            ? GL_UNSIGNED_SHORT_5_5_5_1
            : GL_UNSIGNED_BYTE;

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            internal_format,
            width,
            height,
            0,
            external_format,
            type,
            nullptr);
    }

    void configure_se_shader_if_needed()
    {
        const long this_config = get_video_config();
        if (this_config == scaler_last_config || scaler_ticks == 0)
            return;

        glb::set_uniform("warpX", float(config.video.warpX) / 200.0f);
        glb::set_uniform("warpY", float(config.video.warpY) / 100.0f);

        float invExpandX;
        if (config.video.hires == 0)
            invExpandX = 1.0f / 1.03f;
        else
        {
#if SNES_NTSC_HAVE_SIMD
            invExpandX = 1.0f / 1.01f;
#else
            invExpandX = 1.0f / 1.03f;
#endif
        }

        glb::set_uniform2("invExpand", invExpandX, 1.0f);
        glb::set_uniform(
            "brightboost",
            1.0f + float(config.video.brightboost) / 100.0f);
        glb::set_uniform(
            "noiseIntensity",
            float(config.video.noise) / 100.0f);

        const float vignette =
            config.video.shadow_mask < 2
                ? 0.0f
                : float(config.video.vignette) / 100.0f;
        glb::set_uniform("vignette", vignette);

        float desat_val = float(config.video.desaturate) / 100.0f;
        glb::set_uniform("desat_inv0", 1.0f / (1.0f + desat_val));
        desat_val += float(config.video.desaturate_edges) / 100.0f;
        glb::set_uniform("desat_inv1", 1.0f / (1.0f + desat_val));

        glb::set_uniform(
            "baseOff",
            config.video.shadow_mask == 2
                ? config.video.maskDim / 100.0f
                : 1.0f);
        glb::set_uniform(
            "baseOn",
            config.video.shadow_mask == 2
                ? config.video.maskBoost / 100.0f
                : 1.0f);

        const int mask_size = std::max(3, config.video.mask_size);
        glb::set_uniform("invMaskPitch", 1.0f / float(mask_size));
        glb::set_uniform("inv2MaskPitch", 1.0f / (2.0f * float(mask_size)));
        glb::set_uniform("inv2Height", 1.0f / (2.0f * float(mask_size - 2)));
        glb::set_uniform2("OutputSize", float(dst_rect.w), float(dst_rect.h));
        glb::clear(0.f, 0.f, 0.f, 1.f);

        if (--scaler_ticks == 0)
        {
            scaler_last_config = this_config;
            scaler_ticks = 3;
        }
    }

    void handle_cycle_hotkey()
    {
        const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
        const bool down = keyboard && keyboard[SDL_SCANCODE_F6] != 0;

        if (down && !f6_was_down)
        {
            const int next = pixel_scaler::cycle_hotkey();
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

        const auto [background, foreground] = notification_palette_indices();

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

    void apply_low_factor_detail_preserve()
    {
        const int nearest_weight = factor == 3 ? 25 : 0;
        if (nearest_weight == 0 || source_pixels.empty() || scaled_pixels.empty())
            return;

        const int scaler_weight = 100 - nearest_weight;
        for (int y = 0; y < scaled_height; ++y)
        {
            const int source_y = y / factor;
            const size_t source_row =
                static_cast<size_t>(source_y) * scaler_input_width;
            const size_t scaled_row =
                static_cast<size_t>(y) * scaled_width;

            for (int x = 0; x < scaled_width; ++x)
            {
                const uint32_t nearest =
                    source_pixels[source_row + (x / factor)];
                const uint32_t filtered = scaled_pixels[scaled_row + x];

                const uint32_t r =
                    ((((filtered >> 16) & 0xFFu) * scaler_weight) +
                     (((nearest >> 16) & 0xFFu) * nearest_weight) + 50u) / 100u;
                const uint32_t g =
                    ((((filtered >> 8) & 0xFFu) * scaler_weight) +
                     (((nearest >> 8) & 0xFFu) * nearest_weight) + 50u) / 100u;
                const uint32_t b =
                    (((filtered & 0xFFu) * scaler_weight) +
                     ((nearest & 0xFFu) * nearest_weight) + 50u) / 100u;

                scaled_pixels[scaled_row + x] =
                    0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }

    void apply_se_scanlines_after_scaler()
    {
        const int configured = config.video.scanlines;
        if (configured <= 0 || scaled_height <= 0 || src_height <= 0)
            return;

        const int shift = std::clamp(configured, 1, 3);

        // Map the scaler output back onto the original SE logical raster so a
        // 4x/5x/6x pixel scaler never turns one scanline into a whole dark block.
        for (int y = 0; y < scaled_height; ++y)
        {
            const int se_y = static_cast<int>(
                (static_cast<int64_t>(y) * src_height) / scaled_height);
            if ((se_y & 1) == 0)
                continue;

            uint32_t* row =
                scaled_pixels.data() +
                static_cast<size_t>(y) * scaled_width;

            for (int x = 0; x < scaled_width; ++x)
            {
                const uint32_t p = row[x];
                const uint32_t a = p & 0xFF000000u;
                const uint32_t r = (p >> 16) & 0xFFu;
                const uint32_t g = (p >> 8) & 0xFFu;
                const uint32_t b = p & 0xFFu;
                const uint32_t lum =
                    (77u * r + 150u * g + 29u * b) >> 8;
                const uint32_t rd = r >> shift;
                const uint32_t gd = g >> shift;
                const uint32_t bd = b >> shift;
                const uint32_t out_r =
                    (rd * (255u - lum) + r * lum) >> 8;
                const uint32_t out_g =
                    (gd * (255u - lum) + g * lum) >> 8;
                const uint32_t out_b =
                    (bd * (255u - lum) + b * lum) >> 8;
                row[x] = a | (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    }

    void prepare_rgba_upload()
    {
        if (staging_upload_pixels.size() != scaled_pixels.size())
            staging_upload_pixels.resize(scaled_pixels.size());

        for (size_t i = 0; i < scaled_pixels.size(); ++i)
        {
            const uint32_t p = scaled_pixels[i];
            staging_upload_pixels[i] =
                (p & 0xFF00FF00u) |
                ((p & 0x00FF0000u) >> 16) |
                ((p & 0x000000FFu) << 16);
        }

        std::lock_guard<std::mutex> frame_lock(upload_mutex);
        upload_pixels.swap(staging_upload_pixels);
    }

    void release_scaler_buffers_locked()
    {
        source_pixels.clear();
        scaled_pixels.clear();
        {
            std::lock_guard<std::mutex> frame_lock(upload_mutex);
            upload_pixels.clear();
            staging_upload_pixels.clear();
        }
        scaler_input_width = 0;
        scaler_input_height = 0;
        scaled_width = 0;
        scaled_height = 0;
    }

    bool scaler_path = false;
    bool base_renderer_initialized = false;
    bool f6_was_down = false;
    int active_mode = pixel_scaler::OFF;
    int notification_mode = pixel_scaler::OFF;
    Uint32 notification_until_ms = 0;
    int factor = 1;
    int input_step = 1;
    int scaler_input_width = 0;
    int scaler_input_height = 0;
    int scaled_width = 0;
    int scaled_height = 0;

    long scaler_last_config = -1;
    int scaler_ticks = 3;

    std::mutex scaler_processing_mutex;
    std::mutex upload_mutex;
    std::vector<uint32_t> source_pixels;
    std::vector<uint32_t> scaled_pixels;
    std::vector<uint32_t> upload_pixels;
    std::vector<uint32_t> staging_upload_pixels;
};
