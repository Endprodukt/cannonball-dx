/******************************************************************************
    SDL2 Video Rendering.

    Copyright (c) 2012,2020 Manuel Alfayate and Chris White.

    Modifications for CannonBall-SE are Copyright (c) 2020,2025 James Pearce.

    See license.txt for more details.

*******************************************************************************/

#pragma once

#include "renderbase.hpp"
#include "snes_ntsc.h"
#include <SDL.h>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "gl_backend.hpp"   // tiny ES2 backend

// SDL's Windows backend can reject SDL_SetWindowInputFocus with
// "That operation is not supported". RenderSurface::focus_window already
// shows and raises the window, which is the supported path we need here.
// Treat the unsupported explicit input-focus request as a no-op on Windows.
#ifdef _WIN32
#define SDL_SetWindowInputFocus(window) 0

// VRR/Exclusive test path.
//
// On Windows/ANGLE, create the GLES context only after the SDL window has
// already entered exclusive fullscreen. The normal renderer currently creates
// the context first and changes the display mode afterwards. That can leave
// ANGLE/NVIDIA with presentation resources created for the old windowed state.
//
// Keep this isolated to the test branch. MODE_EXCLUSIVE is value 3 in
// video_settings_t; the helper deliberately takes the mode as an int so this
// header does not need to pull the frontend configuration headers into every
// renderer translation unit.
inline SDL_GLContext cannonball_create_gl_context(SDL_Window* window, int mode)
{
    if (mode == 3)
    {
        const int display_index = SDL_GetWindowDisplayIndex(window);
        SDL_DisplayMode desktop_mode{};

        if (display_index < 0 ||
            SDL_GetDesktopDisplayMode(display_index, &desktop_mode) != 0)
            return nullptr;

        if (SDL_SetWindowDisplayMode(window, &desktop_mode) != 0)
            return nullptr;

        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) != 0)
            return nullptr;

        // Mark the window so the existing post-context exclusive block does
        // not repeat the same mode/fullscreen transition after ANGLE starts.
        SDL_SetWindowData(window, "cannonball.exclusive.precontext",
                          reinterpret_cast<void*>(1));
    }

    return SDL_GL_CreateContext(window);
}

inline int cannonball_set_window_display_mode(SDL_Window* window,
                                               const SDL_DisplayMode* mode)
{
    if (SDL_GetWindowData(window, "cannonball.exclusive.precontext"))
        return 0;

    return SDL_SetWindowDisplayMode(window, mode);
}

inline int cannonball_set_window_fullscreen(SDL_Window* window, Uint32 flags)
{
    if (flags == SDL_WINDOW_FULLSCREEN &&
        SDL_GetWindowData(window, "cannonball.exclusive.precontext"))
        return 0;

    return SDL_SetWindowFullscreen(window, flags);
}

#define SDL_GL_CreateContext(window) \
    cannonball_create_gl_context((window), video_mode)
#define SDL_SetWindowDisplayMode(window, mode) \
    cannonball_set_window_display_mode((window), (mode))
#define SDL_SetWindowFullscreen(window, flags) \
    cannonball_set_window_fullscreen((window), (flags))
#endif

class RenderSurface : public RenderBase
{
public:
    RenderSurface();
    ~RenderSurface();

    // our GLES context
    SDL_GLContext glContext = nullptr;

    bool init(int src_width, int src_height,
              int scale,
              int video_mode,
              int scanlines);
    void swap_buffers();
    void disable();
    void focus_window() override;
    bool start_frame() {return true;};
    bool finalize_frame();
    void draw_frame(uint16_t* pixels, int fastpass);

protected:
    // SDL2 window
    SDL_Window* window = 0;

    SDL_Surface* overlaySurface = nullptr;
    SDL_Surface* GameSurface[2] = {nullptr, nullptr};

    int current_game_surface = 0;
    void* GameSurfacePixels = nullptr;
    uint32_t* overlaySurfacePixels = nullptr;
    uint32_t FrameCounter = 0; // enough space for over 2 years of continuous operation at 60fps

    // SDL2 texture
    SDL_Texture *game_tx = 0;        // game image
    SDL_Texture *overlay = 0;        // CRT curved edge mask

    // SDL2 blitting rects for hw scaling
    // ratio correction using SDL_RenderCopy()
    SDL_Rect src_rect;
    SDL_Rect rgb_rect;
    SDL_Rect dst_rect;

    // image position control (0,0 = top left; calculated in image scaling routine)
    int anchor_x = 0;
    int anchor_y = 0;

    // internal functions
    void create_buffers();
    void destroy_buffers();
    void init_blargg_filter();
    void set_scaling();
    bool init_sdl(int video_mode);
    void init_overlay();
    long get_video_config();
    int  get_blargg_config();
    void blargg_filter(uint16_t* pixels, uint32_t* outputPixels, int section);

    // constants
    const int BPP = 32;

    // Blargg filter related
    snes_ntsc_setup_t setup;
    snes_ntsc_t* ntsc = 0;
    int snes_src_width;
    int phase;
    int phaseframe;

    // currently configured video settings. These are stored so that a change can be actioned.
    int scale           = 0;
    int flags           = 0;  // SDL flags
    int blargg          = 0;  // current Blargg filter value

    // GLSL shader related settings
    std::string vs;
    std::string fs;

    // processing data
    int Alevel = 255;       // default alpha value for game image

    // working buffers for video processing
    uint32_t* game_pixels = 0;
    uint16_t* rgb_pixels = 0;            // used by Blargg filter

	// Locks due to threaded activity
	std::mutex drawFrameMutex, finalizeFrameMutex, gpuMutex;

    // Locks to enable safe disable() call
    std::atomic<int> activity_counter{0}; // track ongoing iterations
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> shutting_down{false};

    // Synchronize fastpass (top/bottom) halves per frame
    std::mutex                fastpassMutex;
    std::condition_variable   fastpassCV;
    int                       fastpassArrivals = 0;   // 0 → 1 → 2 then reset
    bool                      fastpassPostFxDone = false; // lets the first waiter proceed

    // keep track of UI settings changes
    int  last_blargg_config    = 0;
    long last_config           = 0;
    int  last_vignette         = 0;
    int  last_crt_shape_config = 0;

    // and module status
    bool initialised        = false;

    // LUTs for init_overlay()
    std::vector<float> dx1, dx2, dx3, dx4, dx5;
    std::vector<float> dy1, dy2, dy3, dy4, dy5;
};