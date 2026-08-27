/**********************************************************************************
    SDL2 Video Rendering
    Original SDL works Copyright (c) 2012,2020 Manuel Alfayate, Chris White.

    See license.txt for more details.

    This version, for CannnonBall SE, Copyright (c) 2020,2025 James Pearce.

    Provides:
    - GLES display with GLSL shaders via gl_backend.hpp
    - true NTSC (Blargg) filter
    - overlay based screen shape/shadow mask
    - base resolution scanline effect
    - support for multi-threaded processing via double-buffering

***********************************************************************************/

#include <fstream>
#include <iterator>
#include <iostream>
#include <mutex>
#include "rendersurface.hpp"
#include "frontend/config.hpp"
// Aligned Memory Allocation (standard C++17)
#include <new>        // std::align_val_t, ::operator new/delete
#include <cstddef>    // std::size_t
#include <cstdint>
#include <omp.h>
#include <math.h>
#include <cmath>   // std::sqrtf, std::fabs, std::roundf, std::lroundf, etc.
#include <SDL_opengles2.h>
#include <array>   // for std::array

#define VERTEX_SHADER        "res/Cannonball-Shader-Vertex.glsl"
// light shader - curvature/noise/shadow-mask/vignette/brightness-boost
#define FRAGMENT_SHADER      "res/Cannonball-Shader-Fragment.glsl"
// extra light shader - curvature/shadow-mask/brightness-boost only
#define FRAGMENT_SHADER_FAST "res/Cannonball-Shader-Fragment-Fast.glsl"

#ifndef CB_PIXEL_ALIGNMENT
#define CB_PIXEL_ALIGNMENT 64
#endif

RenderSurface::RenderSurface()
{
}

RenderSurface::~RenderSurface()
{
}

bool RenderSurface::init(int source_width, int source_height,
                         int source_scale, int video_mode_requested, int scanlines_requested)
{
    ntsc = (snes_ntsc_t*) malloc( sizeof(snes_ntsc_t) );
    // Can only be called from the thread with the SDL context (usuablly the main thread)
    src_width  = source_width;
    src_height = source_height;
    scale      = source_scale;
    video_mode = video_mode_requested;

    // Capture current settings
    blargg = config.video.blargg;
    setup.saturation = double(config.video.saturation) / 100;
    setup.contrast = double(config.video.contrast) / 100;
    setup.brightness = double(config.video.brightness) / 100;
    setup.sharpness = double(config.video.sharpness) / 100;
    setup.resolution = double(config.video.resolution) / 100;
    setup.gamma = double(config.video.gamma) / 10;
    setup.hue = double(config.video.hue) / 100;

    // ensure we have exclusive access to the SDL context
    std::lock_guard<std::mutex> gpulock(gpuMutex);

    // Initialise Blargg. Comes first as determins working image dimensions.
    last_blargg_config = get_blargg_config();
    init_blargg_filter(); // NTSC filter (CPU based)

    // Initialise SDL
    if (!init_sdl(video_mode)) return false;

    // Get SDL Pixel Format Information
    Rshift = GameSurface[0]->format->Ashift;
    Gshift = GameSurface[0]->format->Bshift;
    Bshift = GameSurface[0]->format->Gshift;
    Ashift = GameSurface[0]->format->Rshift;
    Rmask = GameSurface[0]->format->Amask;
    Gmask = GameSurface[0]->format->Bmask;
    Bmask = GameSurface[0]->format->Gmask;
    Amask = GameSurface[0]->format->Rmask;

    // call other initialisation routines
    init_overlay();       // CRT curved edge mask (applied as a mask by GPU rendering)
    create_buffers();     // used for working space processing image from [game output]->[blargg-filtered]->[renderer input]
    FrameCounter = 0;
    last_config  = 0;

    // signal to workers we're running (e.g. after a video restart)
    shutting_down.store(false, std::memory_order_release);

    initialised = true;
    return true;
}

void RenderSurface::swap_buffers()
{
    // swap the pixel buffers
    std::lock_guard<std::mutex> lock(drawFrameMutex);
    current_game_surface ^= 1;
    GameSurfacePixels = (uint32_t*)GameSurface[current_game_surface]->pixels;
}


void RenderSurface::disable()
{
    // Can only be called from the thread with the SDL context (usuablly the main thread)
    // awaiting threads
    shutting_down.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> guard(drawFrameMutex);
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&] { return activity_counter.load(std::memory_order_acquire) == 0; });

    // ensure we have exclusive access to the SDL context
    std::lock_guard<std::mutex> gpulock(gpuMutex);

    glb::shutdown();

    // delete the SDL‑GL context
    if (glContext) { SDL_GL_DeleteContext(glContext); glContext = nullptr; }

    // destroy window and quit SDL_gpu
    if (window) { SDL_DestroyWindow(window); window = nullptr; }

    // Free the CPU surfaces.
    if (GameSurface[0]) { SDL_FreeSurface(GameSurface[0]); GameSurface[0] = nullptr; }
    if (GameSurface[1]) { SDL_FreeSurface(GameSurface[1]); GameSurface[1] = nullptr; }

    // Release any additional buffers.
    destroy_buffers();
    free(ntsc);

    initialised = false;
}


void RenderSurface::create_buffers() {
    uint32_t pixels;
	uint16_t* t32p;

    // keep 64-byte alignment for SIMD/cache friendliness
    constexpr std::size_t alignment = CB_PIXEL_ALIGNMENT;

    // allocates pixel buffers
    pixels = src_width * src_height;
    std::size_t size = pixels *sizeof(uint16_t);

    rgb_pixels = static_cast<uint16_t*>(::operator new(size, std::align_val_t(alignment)));
    t32p = rgb_pixels;

	while (pixels--) *(t32p++) = 0x0000; // fill with black
}

void RenderSurface::destroy_buffers() {
    // deallocates the pixel buffers
    if (rgb_pixels) {
        ::operator delete(rgb_pixels, std::align_val_t(CB_PIXEL_ALIGNMENT));
    }
    rgb_pixels = nullptr;
}

void RenderSurface::init_blargg_filter()
{
    // Initialises the Blargg NTSC filter effects. This configures the output (s-video/rgb etc) and
    // also pre-calculates the pixel mapping (which is slow), so the shader then runs on a lookup basis
    // in the game (fast).
    if (blargg) {
        // first calculate the resultant image size.
        if (config.video.hires) {
            #if SNES_NTSC_HAVE_SIMD
                // Only compiled when the fast function exists
                snes_src_width = SNES_NTSC_OUT_WIDTH_SIMD(src_width); // for 640px input = 752;
            #else
                snes_src_width = SNES_NTSC_OUT_WIDTH((src_width>>1));
                unsigned check_width = SNES_NTSC_IN_WIDTH(snes_src_width);
                while (check_width < (src_width>>1))
                    check_width = SNES_NTSC_IN_WIDTH(++snes_src_width);
            #endif
        } else {
            snes_src_width = SNES_NTSC_OUT_WIDTH(src_width);
            unsigned check_width = SNES_NTSC_IN_WIDTH(snes_src_width);
            while (check_width < src_width)
                check_width = SNES_NTSC_IN_WIDTH(++snes_src_width);
        }

        // configure selcted filtering type
        switch (config.video.blargg) {
        case video_settings_t::BLARGG_COMPOSITE:
            setup = snes_ntsc_composite;
            break;
        case video_settings_t::BLARGG_SVIDEO:
            setup = snes_ntsc_svideo;
            break;
        case video_settings_t::BLARGG_RGB:
            setup = snes_ntsc_rgb;
            break;
        }
        setup.merge_fields = 0;         // mimic interlacing
        phase = 0;                      // initial frame will be phase 0
        phaseframe = 0;                 // used to track phase at 60 fps
        Alevel = 255;                   // blackpoint
        setup.hue = double(config.video.hue) / 100;
        setup.saturation = double(config.video.saturation) / 100;
        setup.contrast = double(config.video.contrast) / 100;
        setup.brightness = double(config.video.brightness) / 100;
        setup.sharpness = double(config.video.sharpness) / 100;
        setup.gamma = double(config.video.gamma) / 10;
        setup.resolution = double(config.video.resolution) / 100;

        snes_ntsc_init(ntsc, &setup); // configure the library
    }
    else snes_src_width = src_width; // provides constraint to buffer allocation
}


// ----------------------------------------------------------------------------------
// set_scaling - determines the X and Y parameters and image position
// ----------------------------------------------------------------------------------


void RenderSurface::set_scaling()
{
    // Compute source and destination rectangles.
    // These values are computed differently for fullscreen vs. windowed mode.
    if (video_mode == video_settings_t::MODE_FULL ||
        video_mode == video_settings_t::MODE_STRETCH)
    {
        // For fullscreen:
        scn_width = orig_width;
        scn_height = orig_height;

        // Set src_rect dimensions (accounting for a potential Blargg mode, which expands horizontally)
        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.w = (blargg) ? snes_src_width : src_width;
        src_rect.h = src_height;

        // Determine destination rectangle:
        dst_rect.x = 0;
        dst_rect.y = 0;
        dst_rect.h = scn_height;
        dst_rect.w = scn_width;

        if (video_mode == video_settings_t::MODE_FULL) {
            // Maintain game aspect ratio:
            int correct_height = int(float(src_height) * float(scn_width) / float(src_width));
            int correct_width  = int(float(src_width) * float(scn_height) / float(src_height));
            if (correct_height > dst_rect.h) {
                // re-scale width, to leave black bars either side and image full height on screen)
                std::cout << "Image centered horizontally, ";
                dst_rect.w = int(float(src_width) * float(scn_height) / float(src_height));
                dst_rect.x = (scn_width - dst_rect.w) >> 1;
                anchor_x   = dst_rect.x;
                anchor_y   = 0;
            }
            if (correct_width > dst_rect.w) {
                // re-scale height, to leave black bars top and bottom and image full width)
                std::cout << "Image centered vertically, ";
                dst_rect.h = correct_height;
                dst_rect.y = (scn_height - dst_rect.h) >> 1;
                anchor_y   = dst_rect.y;
                anchor_x   = 0;
            }
        }
        std::cout << "Image anchor point: " << anchor_x << "," << anchor_y << "\n";
        SDL_ShowCursor(SDL_DISABLE);
    }
    else  // Windowed mode
    {
        video_mode = video_settings_t::MODE_WINDOW;
        // Start with a desired scale (e.g. 4x the native resolution)
        scn_width = src_width * scale;
        scn_height = src_height * scale;
        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.w = (blargg == 0) ? src_width : snes_src_width;
        src_rect.h = src_height;

        dst_rect.x = 0;
        dst_rect.y = 0;
        dst_rect.w = scn_width;
        dst_rect.h = scn_height;
        SDL_ShowCursor(SDL_ENABLE);
    }
}

// ----------------------------------------------------------------------------------
// SDL Initialisation
// ----------------------------------------------------------------------------------

bool RenderSurface::init_sdl(int video_mode)
{
    // First, determine our source and destination dimensions.
    // RenderBase::sdl_screen_size() should set orig_width and orig_height.
    if (!RenderBase::sdl_screen_size())
        return false;

    // Determine the image scaling parameters and image position
    set_scaling();

    // --------------------------------------------------------
    // Request an OpenGL ES2 context (for desktop & mobile)
    // --------------------------------------------------------
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");   // tell SDL to prefer ANGLE/GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,   2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,   0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,            1);

    // Create a window manually so that it can be closed on video restart (e.g. Blargg on/off)
    // Now create our window (with an OpenGL flag)

    window = SDL_CreateWindow("CannonBall DX",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        scn_width, scn_height, SDL_WINDOW_OPENGL);

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create the ES context
    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "Failed to create GLES context: " << SDL_GetError() << std::endl;
        return false;
    }

    // MODE_FULL and MODE_STRETCH use borderless desktop fullscreen. A real
    // MODE_WINDOW must keep the scaled SDL window created above instead of
    // being promoted to fullscreen unconditionally.
    if (video_mode == video_settings_t::MODE_FULL ||
        video_mode == video_settings_t::MODE_STRETCH)
    {
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
        {
            std::cerr << "Failed to enter fullscreen mode: "
                      << SDL_GetError() << std::endl;
            return false;
        }
    }

    // Fix the GL viewport to the actual drawable size in both fullscreen and
    // windowed modes.
    glb::on_drawable_resized();

    // --- Tiny ES2 backend init (replaces SDL_gpu) ---
    auto loadTextFile = [](const char* path)->std::string {
        std::ifstream f(path, std::ios::binary);
        if (!f) return {};
        return std::string((std::istreambuf_iterator<char>(f)), {});
    };
    if (config.video.shader_mode == 0) {
        // shader not enabled - force pass-through shader in gl_backend
        vs.clear();
        fs.clear();
    } else {
        // shader is enabled
        vs = loadTextFile(VERTEX_SHADER);
        fs = loadTextFile((config.video.shader_mode == 2) ? FRAGMENT_SHADER : FRAGMENT_SHADER_FAST);
        if (vs.empty() || fs.empty()) {
            std::cerr << "Failed to load shader sources.\n";
            return false;
        }
    }
    // Initialize GL backend
    if (blargg)
        glb::set_game_pixel_format(glb::State::PixFmt::RGBA);
    else
        glb::set_game_pixel_format(glb::State::PixFmt::RGB555);

    if (!glb::init(window,
                   /*gameW*/    src_rect.w, /*gameH*/    src_rect.h,
                   /*overlayW*/ dst_rect.w, /*overlayH*/ dst_rect.h,
                   vs.empty() ? nullptr : vs.c_str(),
                   fs.empty() ? nullptr : fs.c_str(),
                   /*createOffscreen=*/false)) {
        std::cerr << "gl_backend init failed.\n";
        return false;
    }

    Uint32 window_format = SDL_GetWindowPixelFormat(window);
    printf("Window Pixel Format: %s (0x%08X)\n", SDL_GetPixelFormatName(window_format), window_format);

    //--------------------------------------------------------
    // Create CPU surfaces for the game image.
    //--------------------------------------------------------

    // Double-buffered game surfaces
    auto pix_format = (blargg) ? SDL_PIXELFORMAT_RGBA8888 : SDL_PIXELFORMAT_RGB555;
    int  bpp        = (blargg) ? 32 : 16;
    GameSurface[0] = SDL_CreateRGBSurfaceWithFormat(0, src_rect.w, src_rect.h, bpp, pix_format);
    GameSurface[1] = SDL_CreateRGBSurfaceWithFormat(0, src_rect.w, src_rect.h, bpp, pix_format);
    if (!GameSurface[0] || !GameSurface[1]) {
        std::cerr << "SDL Surface creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    current_game_surface = 0;
    if (blargg) {
        GameSurfacePixels = static_cast<uint32_t*>(GameSurface[current_game_surface]->pixels);
    } else {
        GameSurfacePixels = static_cast<uint16_t*>(GameSurface[current_game_surface]->pixels);
    }

    glb::set_swap_interval(config.video.vsync);
    //glb::auto_configure_pixel_formats_from_surfaces(GameSurface[0], overlaySurface);

    Uint32 black_color = SDL_MapRGBA(GameSurface[0]->format, 0, 0, 0, 0);
    SDL_FillRect(GameSurface[0], NULL, black_color);
    SDL_FillRect(GameSurface[1], NULL, black_color);

    // screen_pixels = static_cast<uint32_t*>(surface->pixels);
    return true;
}



//-----------------------------------------------------------------
// Overlay mask. This pre-calculated mask combines CRT shape,
// vignette and CRT shadow mask effect in one pass, It is overlayed
// on each frame, reducing GPU demand compared to a pixel shader
// approach.
//-----------------------------------------------------------------

int find_circle_intersection_(float x1, float y1, float r1,
    float x2, float y2, float r2,
    float* ix, float* iy) {
    // returns the top-left intersection between two circles
    // whose centres are Xn,Yn and having radii Rn
    // Distance between the centers
    double d = sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));

    // Check if there are no intersections
    if (d > r1 + r2 || d < fabs(r1 - r2) || d == 0) {
        *ix = 0;
        *iy = 0;
        return 0;  // No intersection
    }

    // Distance from the center of the first circle to the midpoint of the intersection line
    double a = (pow(r1, 2) - pow(r2, 2) + pow(d, 2)) / (2 * d);

    // Height from midpoint to each intersection; clamp to avoid sqrt(-0).
    double h = sqrt(pow(r1, 2) - pow(a, 2));

    // Midpoint on the line connecting the centers
    double px = x1 + a * (x2 - x1) / d;
    double py = y1 + a * (y2 - y1) / d;

    // Offset of the intersection points from the midpoint
    double offset_x = h * (y2 - y1) / d;
    double offset_y = h * (x2 - x1) / d;

    // Intersection points
    float ix1 = px + offset_x;
    float iy1 = py - offset_y;
    float ix2 = px - offset_x;
    float iy2 = py + offset_y;

    // Return smallest values
    if (ix1 < ix2) {
        *ix = ix1;
        *iy = iy1;
    } else {
        *ix = ix2;
        *iy = iy2;
    }

    return 1;  // Intersection found
}


int find_circle_intersection(float x1, float y1, float r1,
                             float x2, float y2, float r2,
                             float* ix, float* iy)
{
    // Return the "top-left" intersection in screen coords (min x, then min y).
    // All intermediate math in double for stability at 0–50k scale.
    constexpr double EPS = 1e-6;

    const double dx = double(x2) - double(x1);
    const double dy = double(y2) - double(y1);
    const double d2 = dx*dx + dy*dy;
    const double d  = std::sqrt(d2);

    // No intersection: separate, contained, or (near) coincident centers.
    if (d > double(r1) + double(r2) + EPS ||
        d + std::fabs(double(r1) - double(r2)) < EPS)   // contained or coincident
    {
        *ix = 0.0f; *iy = 0.0f;
        return 0;
    }

    // Distance from c1 to the chord midpoint
    const double r1d = double(r1);
    const double r2d = double(r2);
    const double a   = (r1d*r1d - r2d*r2d + d2) / (2.0 * d);

    // Height from midpoint to each intersection; clamp to avoid sqrt(-0).
    double h2 = r1d*r1d - a*a;
    if (h2 < 0.0) h2 = 0.0;
    const double h = std::sqrt(h2);

    // Point along the center line
    const double ux = dx / d;
    const double uy = dy / d;
    const double px = double(x1) + a * ux;
    const double py = double(y1) + a * uy;

    // Offsets perpendicular to center line
    const double offx =  h * (-uy);
    const double offy =  h * ( ux);

    // Two intersections
    const double xA = px + offx, yA = py + offy;
    const double xB = px - offx, yB = py - offy;

    // Choose "top-left": smaller x; if ~equal, smaller y.
    double rx, ry;
    if (xA < xB - EPS || (std::fabs(xA - xB) <= EPS && yA < yB)) {
        rx = xA; ry = yA;
    } else {
        rx = xB; ry = yB;
    }

    *ix = float(rx);
    *iy = float(ry);
    return 1;
}



void RenderSurface::init_overlay()
{
    // This function builds out the mask as an ALPHA8 blend mask (FF=transparent, 0=black).
    // This is called by init(), all also by draw_frame() if the user has changed a setting.
    // Texture dimensions must be previously defined (by init_textures)
    // This function is computationally expensive.

    // Check if overlay is disabled. This sets the overlay in the shader to a 1:1 white
    // which reduces RAM bandwidth required e.g. for Pi2.
    if (!config.video.crt_shape && !config.video.vignette) {
        glb::clear_overlay_texture();
        return;
    }

    // get settings
    int crt_shape_config = config.video.crt_shape +
                           config.video.warpX +
                           config.video.warpY;

    // create buffer. Fill is 0xFF (clear)
	int pixels = dst_rect.w * dst_rect.h;
    std::vector<uint8_t> a8(pixels, 0xFF);

    if (1) {
        // vignette and shape
        const uint32_t vignette_target = int((float(config.video.vignette) * 255.0 / 100.0));
        const float midx = float(dst_rect.w >> 1);
        const float midy = float(dst_rect.h >> 1);
        const float dia = sqrt(((midx * midx) + (midy * midy)));
        const float outer = dia * 1.00;
        const float outer2 = outer * outer;
        const float inner = dia * 0.30;
        const float inner2 = inner * inner;
        const float outer_less_inner2 = ((outer - inner) * (outer - inner));
        const float total_black = 0.0;

        // Blacked-out corners and top/bottom fade and curved edges
        const float corner_radius = 0.02 * dia;    // this is the radius of the rounded corner
        const float edge_radius = 0.01 * dia;      // this amount will be faded to black, creating a smooth edge to the curve
        const float edge_radius2 = edge_radius * edge_radius;
        const float crt_curve_radius_x = dia * 12.0 * float(16-config.video.warpX) / 16.0;
        const float crt_curve_radius_x2 = crt_curve_radius_x * crt_curve_radius_x;
        const float crt_curve_radius_y = dia * 18.0 * float(18-config.video.warpY) / 18.0;
        const float crt_curve_radius_y2 = crt_curve_radius_y * crt_curve_radius_y;
        float corner_x = 0;
        float corner_y = 0;

        // calculate intersection of CRT curves at top left
        float x_intersection, y_intersection;
        find_circle_intersection(crt_curve_radius_x, midy, crt_curve_radius_x,
            midx, crt_curve_radius_y, crt_curve_radius_y, &x_intersection, &y_intersection);

        // calculate intersetion of curves at top left less edge and corner radius,
        // this will be the centre of the corner curve if configured
        find_circle_intersection(crt_curve_radius_x, midy, (crt_curve_radius_x - edge_radius - corner_radius),
            midx, crt_curve_radius_y, (crt_curve_radius_y - edge_radius - corner_radius), &corner_x, &corner_y);

        int x_intersect = int(x_intersection);
        int y_intersect = int(y_intersection);

        if (!initialised || (last_crt_shape_config != crt_shape_config)) {
            // build LUTs
            int span_w = (dst_rect.w >> 1) + 1;
            int span_h = (dst_rect.h >> 1) + 1;
            dx1.resize(span_w+1); dx2.resize(span_w+1); dx3.resize(span_w+1); dx4.resize(span_w+1); dx5.resize(span_w+1);
            dy1.resize(span_h+1); dy2.resize(span_h+1); dy3.resize(span_h+1); dy4.resize(span_h+1); dy5.resize(span_h+1);

            for (int idx = 0; idx <= span_w; idx++) {
                dx1[idx] = (midx - float(idx)) * (midx - float(idx));
                dx2[idx] = (crt_curve_radius_x - float(idx)) * (crt_curve_radius_x - float(idx));
                dx4[idx] = ((x_intersect + edge_radius) - float(idx)) * ((x_intersect + edge_radius) - float(idx));
                dx5[idx] = (corner_x - float(idx)) * (corner_x - float(idx));
            }

            for (int idx = 0; idx <= span_h; idx++) {
                dy1[idx] = (midy - float(idx)) * (midy - float(idx));
                dy2[idx] = (crt_curve_radius_y - float(idx)) * (crt_curve_radius_y - float(idx));
                dy4[idx] = ((y_intersect + edge_radius) - float(idx)) * (y_intersect + edge_radius - float(idx));
                dy5[idx] = (corner_y - float(idx)) * (corner_y - float(idx));
            }
        }

        // calculate the mask values
#pragma omp parallel for
        for (int y = 0; y <= (dst_rect.h >> 1); y++) {
            uint8_t* scnlp1 = a8.data() + (y * dst_rect.w);
            uint8_t* scnlp2 = scnlp1 + dst_rect.w - 1;
            uint8_t* scnlp3 = a8.data() + ((dst_rect.h - y - 1) * dst_rect.w);
            uint8_t* scnlp4 = scnlp3 + dst_rect.w - 1;
            int y_pos = (y < midy) ? y : dst_rect.h - y;
            uint32_t shadeval, maskval;

            for (int x = 0; x <= (dst_rect.w >> 1); x++) {
                // mask is symetrical so we only need to calculate half

                shadeval = 0xff; // clear
                int x_pos = (x < midx) ? x : dst_rect.w - x;
                int value_set = 0;

                // Calculate the location of the current pixel relative to:
                // d1 - the screen centre (midx, midy)
                // d2 - the center of the CRT curve on x axis (crt_curve_radius_x, midy)
                // d3 - the center of the CRT curve on y axis (midx, crt_curve_radius_y)
                // d4 - the intersection of the CRT curves at the top left (x_intersect, y_intersect)
                // d5 - the corner radius centre (corner_x, corner_y)

                // These are calculated as:
                // float d1 = sqrt(((midx - float(x_pos)) * (midx - float(x_pos))) + ((midy - float(y_pos)) * (midy - float(y_pos))));
                // float d2 = sqrt(((crt_curve_radius_x - float(x_pos)) * (crt_curve_radius_x - float(x_pos))) + ((midy - float(y_pos)) * (midy - float(y_pos))));
                // float d3 = sqrt(((midx - float(x_pos)) * (midx - float(x_pos))) + ((crt_curve_radius_y - float(y_pos)) * (crt_curve_radius_y - float(y_pos))));
                // float d4 = sqrt((((x_intersect + edge_radius) - float(x_pos)) * ((x_intersect + edge_radius) - float(x_pos))) + (((y_intersect + edge_radius) - float(y_pos)) * (y_intersect + edge_radius - float(y_pos))));
                // float d5 = sqrt(((corner_x - float(x_pos)) * (corner_x - float(x_pos))) + ((corner_y - float(y_pos)) * (corner_y - float(y_pos))));
                //
                // sqrt is very expensive on ARMv6/v7 hence we minimise the number of calls we need to made.

                // first apply overall vignette effect based on distance from centre (d1)
                float d1sq = dx1[x_pos] + dy1[y_pos];
                if (d1sq >= outer2) {
                    // black out beyond this region
                    shadeval = int(total_black);
                    continue; // early-out
                }
                else if ((d1sq >= inner2) && (config.video.shader_mode < 2)) {
                    // vignette handled in GPU shader for all masks except 0 (off) and 1 (overlay based, code below)
                    // intermediate value; increase intensity with square of distance to avoid visible edge
                    float d1 = std::sqrt(d1sq);
                    shadeval = 255 - uint32_t(round(((vignette_target) * ((d1 - inner) * (d1 - inner)) /
                        outer_less_inner2)));
                }
                else shadeval = 0xff; // no dimming

                // create rounded corners about the intersection point adjusted for the corner radius
                // if that point could be determined
                if ((corner_x > 1.0) && (corner_y > 1.0)) {
                    if ((x_pos <= corner_x) &&
                        (y_pos <= corner_y)) {
                        // apply curve over existing vignette
                        float d5 = std::sqrt(dx5[x_pos] + dy5[y_pos]);
                        shadeval = (shadeval *
                            (d5 >= (edge_radius + corner_radius) ? int(total_black) :
                                (d5 > corner_radius ?
                                        (uint32_t(round((255 * fabs(edge_radius - (d5 - corner_radius))) / edge_radius)))
                                        : 255))) >> 8;
                        value_set = 1;
                    }
                }
                else {
                    // follow the edge contour around the corners as the specified corner_radius
                    // did not intersect with both curves (i.e. was too small)
                    if ((x_pos <= (int(x_intersect + edge_radius))) &&
                        (y_pos <= (int(y_intersect + edge_radius)))) {
                        float d4sq = dx4[x_pos] + dy4[y_pos];
                        if (d4sq < edge_radius2) {
                            float d4 = std::sqrt(d4sq);
                            shadeval = (shadeval *
                                (uint32_t(round((255 * fabs(edge_radius - d4)) / edge_radius)))
                                ) >> 8; // apply curve over existing vignette
                            value_set = 1;
                        }
                    }
                }

                if (value_set == 0) {
                    // remove horizontal 'ears' at each corner
                    if ((y_pos <= int(y_intersect + edge_radius)) &&
                        (x_pos <= int(x_intersect + edge_radius))) shadeval = int(total_black);

                    // next apply the curved edge effect based on distance from the CRT curve (d2 and d3)
                    // first use d2 (x axis) as this will be larger
                    if (x_pos <= (x_intersect + edge_radius)) {
                        // somewhere on curve on left edge
                        //float d2 = sqrt(dx2[x_pos] + dy1[y_pos]);
                        float d2sq = dx2[x_pos] + dy1[y_pos];
                        if (d2sq >= crt_curve_radius_x2) {
                            // black out beyond this region
                            shadeval = int(total_black);
                        }
                        else {
                            float d2 = std::sqrt(d2sq);
                            if ((crt_curve_radius_x - d2) < edge_radius) {
                                // apply the curve, x255 then >> 8 saves on floating-point division
                                shadeval = (shadeval *
                                    (uint32_t(round((255 * fabs(crt_curve_radius_x - d2)) / edge_radius)))
                                    ) >> 8; // apply curve over existing vignette
                            }
                        } // else unaffected
                    }
                    else {
                        // next use d3 (y-axis curve)
                        if (y_pos <= (y_intersect + edge_radius)) {
                            // somewhere on curve on top edge
                            float d3sq = dx1[x_pos] + dy2[y_pos];
                            if ((d3sq >= crt_curve_radius_y2)) {
                                // black out beyond this region
                                shadeval = int(total_black);
                            }
                            else {
                                float d3 = std::sqrt(d3sq);
                                if ((crt_curve_radius_y - d3) < edge_radius) {
                                // apply the curve
                                shadeval = (shadeval *
                                    (uint32_t(round((255 * fabs(crt_curve_radius_y - d3)) / edge_radius)))
                                    ) >> 8; // apply curve over existing vignette
                                }
                            }
                        }
                    }
                }
                // store the calculated mask value in the texture
                *(scnlp1++) = shadeval; // top-left
                *(scnlp2--) = shadeval; // top-right
                *(scnlp3++) = shadeval; // bottom-left
                *(scnlp4--) = shadeval; // bottom-right
            }
        }
    }

/*  Mask is now handled on the shader
    // Overlay with CRT Mask, suitable for resource constrained targets as combines mask, vignette and shape in single
    // pass. However, the quality of the mask and vignette effects are reduced.
    // The entire image is processed in this loop
    if (config.video.shadow_mask == 1) {
        // small square mask effect but without rgb split, for standard screens like 1280x1024
        int current = 0;
        uint8_t* scnlp = a8.data();
        uint32_t dimval;
        uint32_t dimval_h = (config.video.maskDim) * 255 / 100; // percent that shows through
        uint32_t dimval_v = dimval_h * dimval_h / 255; // verticals are darker

        for (int y = 0; y < dst_rect.h; y++) {
            current = 0;
            for (int x = 0; x < dst_rect.w; x++) {
                dimval = 0xFF; // reset to no further dimming
                if ((current == 2) || (current == 5))
                    dimval = dimval_v; // dark column
                else
                {
                    if ((y & 0x01) == 0) {
                        // dim alternate pixels on this whole row
                        if (current < 2) dimval = dimval_h;
                    }
                    if ((y & 0x01) == 1) {
                        // dim alternate pixels on this whole row
                        if ((current > 2) && (current < 5)) dimval = dimval_h;
                    }
                }
                if (++current == 6) current = 0;

                // fast *dimval/256:
                uint32_t t = static_cast<uint32_t>(*scnlp) * dimval;
                *scnlp = static_cast<uint8_t>((t + 128 + (t >> 8)) >> 8);
                ++scnlp;
            }
        }
    }
    */

    // Upload overlay pixels to GPU overlay texture
    glb::set_overlay_pixel_format_a8();
    glb::reallocate_overlay_storage();

    glb::update_overlay_texture( a8.data(),dst_rect.w,dst_rect.w,dst_rect.h );
    //                           overlaySurfacePixels,pitchBytes*4, w, h

    // save current settings
    last_vignette         = config.video.vignette;
    last_crt_shape_config = crt_shape_config;
}



long RenderSurface::get_video_config() {
    return                ( config.video.hires          +
                            config.video.shader_mode    +
                            config.video.crt_shape      +
                            config.video.x_offset       +
                            config.video.y_offset       +
                            config.video.blargg         +
                            config.video.warpX          +
                            config.video.warpY          +
                            config.video.brightboost    +
                            config.video.noise          +
                            config.video.vignette       +
                            config.video.desaturate     +
                            config.video.desaturate_edges +
                            config.video.shadow_mask    +
                            (config.video.maskDim*2)    +
                            config.video.maskBoost      +
                            config.video.mask_size      +
                            dst_rect.w + dst_rect.h );
}


bool RenderSurface::finalize_frame()
{
	// This function is called after the frame has been rendered, and is responsible for
	// updating the screen with the new frame. It also applies post-processing effects
	// via GPU shader and CRT edge overlay if enabled.

    if (config.videoRestartRequired) return true;

    // check is disable() is waiting
    if (shutting_down.load(std::memory_order_acquire)) return true;
    activity_counter.fetch_add(1, std::memory_order_acq_rel);

    // ensure we have exclusive access to the SDL context
    std::lock_guard<std::mutex> gpulock(gpuMutex);

    int game_width = src_rect.w;
    int game_height = src_rect.h;

    // Whether to use off-screen target
    int offscreen_rendering = 0;

    if (FrameCounter++ == 60) FrameCounter = 0;

    // *** SHADER DRAW ***

    SDL_Surface* localGameSurface;
    {
        std::lock_guard<std::mutex> lock(drawFrameMutex);
        const int idx = current_game_surface ^ 1;
        localGameSurface = GameSurface[idx]; // Latch the SDL_Surface*
    }

    // Upload this frame’s CPU pixels to the GPU
    glb::update_game_texture(
        localGameSurface->pixels,
        localGameSurface->pitch,
        game_width,
        game_height
    );

    /* == Configure shader options ('uniforms') == */

    static long last_config = 0;
    static int  ticks = 3;
    // check for any settings changes
    if (1)
    {
        long this_config = get_video_config();
        if ((this_config!=last_config) && ticks) {
            // Send updated config to the shader
            glb::set_uniform("warpX",           float(config.video.warpX) / 200.0f);
            glb::set_uniform("warpY",           float(config.video.warpY) / 100.0f);

            float invExpandX;
            if (config.video.hires==0) {
                // add 3% width to the source as the non-SIMD blargg filter leaves a black bar on the right
                invExpandX = 1 / 1.03;
            } else {
                #if SNES_NTSC_HAVE_SIMD
                invExpandX = 1 / 1.01;
                #else
                    // add 3% width to the source as the non-SIMD blargg filter leaves a black bar on the right
                    invExpandX = 1 / 1.03;
                #endif
            }
            float invExpandY = 1.0;
            glb::set_uniform2("invExpand",      invExpandX, invExpandY);

            glb::set_uniform("brightboost",     1 + (float(config.video.brightboost) / 100.0f));
            glb::set_uniform("noiseIntensity",  float(config.video.noise) / 100.0f);

            float vignette = (config.video.shadow_mask < 2) ? 0.0f : float(config.video.vignette) / 100.0f;
            glb::set_uniform("vignette",        vignette);

            float desat_val = (config.video.desaturate) / 100.0f;
            glb::set_uniform("desat_inv0",      (1.0f / (1.0f + desat_val)));
            desat_val += (config.video.desaturate_edges) / 100.0f;
            glb::set_uniform("desat_inv1",      (1.0f / (1.0f + desat_val)));

            glb::set_uniform("baseOff",         (config.video.shadow_mask==2 ? (config.video.maskDim/100.0f)   : 1.0f));
            glb::set_uniform("baseOn",          (config.video.shadow_mask==2 ? (config.video.maskBoost/100.0f) : 1.0f));
            int this_mask_size = std::max(3, config.video.mask_size);
            glb::set_uniform("invMaskPitch",    (1.0f /    float(this_mask_size)) );
            glb::set_uniform("inv2MaskPitch",   (1.0f / (2*float(this_mask_size))) );
            glb::set_uniform("inv2Height",      (1.0f / (2*float((this_mask_size-2)))) );

            glb::set_uniform2("OutputSize",     float(dst_rect.w), float(dst_rect.h));
            glb::clear(/*rgba*/ 0.f, 0.f, 0.f, 1.f);
            if (--ticks==0) {
                last_config = this_config;
                ticks = 3;
            }
        }
        glb::set_uniform2("u_Time",         (float(FrameCounter) / 60.0), 0.0f );
    }

    int this_crt_shape_config = config.video.crt_shape +
                                config.video.warpX +
                                config.video.warpY;

    if ( ((config.video.vignette != last_vignette) && (config.video.shader_mode < 2)) ||
          (this_crt_shape_config != last_crt_shape_config) ) {
        // update the overlay if:
        // 1. We're using it for vignette and the user has changed the setting, or
        // 2. The CRT shape setting has been changed, or
        // 3. The warp settings have been changed.
        init_overlay();
    }

    /* == blit the game image. This processes it with the shader into the configured target buffer == */

    // set image position and size
    int x0 = anchor_x + config.video.x_offset;
    int y0 = anchor_y + config.video.y_offset;
    glb::set_present_rect_pixels_top_left(x0, y0, dst_rect.w, dst_rect.h);
    glb::set_overlay_rect_pixels_top_left(x0, y0, dst_rect.w, dst_rect.h);
    // Draw to the window; gl_backend handles overlay multiply as a second pass.
    glb::draw( /*useOffscreen=*/(offscreen_rendering==1),
               /*drawOverlay=*/((config.video.crt_shape != 0)||(config.video.shadow_mask==1)) );

    // ultimately calls SDL_GL_SwapWindow
    glb::present();

    // notify disable() that we're done
    activity_counter.fetch_sub(1, std::memory_order_acq_rel);
    std::unique_lock<std::mutex> lock(mtx);
    cv.notify_all();  // In case disable() is waiting

    return true;
}


void RenderSurface::blargg_filter(uint16_t* gamePixels, uint32_t* outputPixels, int section)
{
    // Processes either half of the image:
    //   top half when section = 0
    //   bottom half when section = 1
    //   entire image when section = -1

    long src_pixel_count = src_width * src_height;
    long dst_pixel_count = snes_src_width * src_height;
    long block_height    = src_height;

    int this_section = section;
    if (this_section >= 0) {
        src_pixel_count = src_pixel_count >> 1;
        dst_pixel_count = dst_pixel_count >> 1;
        block_height = block_height >> 1;
    } else {
        this_section = 0;
    }

    uint16_t* spix = gamePixels + (this_section * src_pixel_count); // S16 Output
    uint16_t* bpix = rgb_pixels + (this_section * src_pixel_count); // converted colour buffer

    if (blargg) {
        // convert pixel data to format used by Blarrg filtering code

        // translate game image to lookup format that Blargg filter will use
        long pixel_count = (src_pixel_count) >> 2; // unroll 4:1
		while (pixel_count--) {
            // translate game image to lookup format that Blargg filter will use to
            // convert to RGB output levels in one step based on pre-defined S16-correct DAC output values
            *(bpix+0) = rgb_blargg[*(spix+0)];
            *(bpix+1) = rgb_blargg[*(spix+1)];
            *(bpix+2) = rgb_blargg[*(spix+2)];
            *(bpix+3) = rgb_blargg[*(spix+3)];
            bpix += 4;
            spix += 4;
        }

        long output_pitch = (snes_src_width << 2); // 4 bytes-per-pixel (8/8/8/8)

        // Set pointers
        bpix = rgb_pixels + (this_section * src_pixel_count);
        uint32_t* tpix = outputPixels + (this_section * dst_pixel_count);

        // Calculated alpha mask
        uint32_t Ashifted = uint32_t(Alevel);// << Ashift;

        // Now call the blargg code, to do the work of translating S16 output to RGB
        if (config.video.hires) {
            // hi-res
            #if SNES_NTSC_HAVE_SIMD
                // Only compiled when the fast function exists
                snes_ntsc_blit_hires_fast(ntsc, bpix, long(src_width), phase, src_width,
                                          block_height, tpix, output_pitch, Ashifted);
            #else
                snes_ntsc_blit_hires(ntsc, bpix, long(src_width), phase, src_width,
                                     block_height, tpix, output_pitch, Ashifted);
            #endif
        }
        else {
            // standard res processing
            snes_ntsc_blit(ntsc, bpix, long(src_width), phase,
                src_width, block_height, tpix, output_pitch, Ashifted);
        }
    }
}



#include <stdint.h>
#include <stddef.h>
/**
 * Dim every other scanline of a packed RGB565 image by 1/2, 1/4 or 1/8.
 *
 * @param pixels Pointer to uint16_t RGB565 data (size = width*height).
 * @param width  Image width in pixels.
 * @param height Image height in pixels.
 * @param shift  Right‐shift amount: 1 → ½, 2 → ¼, 3 → ⅛.
 */
static inline void apply_scanlines_(uint32_t *pixels,
                                     size_t width,
                                     size_t height,
                                     uint8_t shift,
                                     uint8_t AShift)
{
    // Precomputed masks to clear each channel's low 'shift' bits
    // so we can then shift the whole word - this avoids unpacking.
    // This is also likely to be auto-vectorised at O3.
    //   masks[1] = 0xF7DE; // clear bits 0,5,11  → 1/2
    //   masks[2] = 0xE79C; // clear bits 0–1,5–6,11–12 → 1/4
    //   masks[3] = 0xC718; // clear bits 0–2,5–7,11–13 → 1/8
    static const uint32_t masks[4] = {
        0xFFFFFFFFu,  // no dim
        0xFEFEFEFEu,  // >>1
        0xFCFCFCFCu,  // >>2
        0xF8F8F8F8u,  // >>3
    };

    uint32_t mask = masks[shift & 3];
    uint32_t AMask = 0xFF << AShift;
    for (size_t y = 1; y < height; y += 2) {
        uint32_t *row = pixels + y * width;
        for (size_t x = 0; x < width; x++) {
            uint32_t p = *row;
            uint32_t AVal = p & AMask;
            *(row++) = ((p & mask) >> shift) | AMask;
        }
    }
}


// CPU-side scanlines. These are applied to (and so align with) the game image, which generally looks better
// Processes either half of the image:
//   top half when section = 0
//   bottom half when section = 1
//   entire image when section = -1

// shift masks
static const uint32_t masks[4] = { 0xFFFFFFFFu, 0xFEFEFEFEu, 0xFCFCFCFCu, 0xF8F8F8F8u };

static inline void apply_scanlines(uint32_t *pixels,
                                     size_t width, size_t height,
                                     uint8_t shift,
                                     uint8_t Rshift, uint8_t Gshift, uint8_t Bshift, uint8_t Ashift,
                                     int     section)
{
    uint32_t mask   = masks[shift & 3];
    uint32_t AMask  = 0xFFu << Ashift;   // preserve alpha bits

    const size_t block_height = (section >= 0 ? (height >> 1) : height);
    const size_t starty = (section == 1 ? block_height : 0);
    const size_t endy   = starty + block_height;

    for (size_t y = (starty+1); y < endy; y += 2) {
        uint32_t *row = pixels + y * width;
        for (size_t x = 0; x < width; x++, row++) {
            uint32_t p = *row;

            // 1) unpack each channel using its shift
            uint8_t r = (p >> Rshift) & 0xFF;
            uint8_t g = (p >> Gshift) & 0xFF;
            uint8_t b = (p >> Bshift) & 0xFF;
            uint8_t a = (p >> Ashift) & 0xFF;

            // 2) compute perceptual luminance (0–255)
            uint8_t lum = ( ( 77 * r
                            +150 * g
                            + 29 * b ) >> 8 );

            // 3) apply the scanline “dim” to each channel
            uint8_t rd = r >> shift;
            uint8_t gd = g >> shift;
            uint8_t bd = b >> shift;

            // 4) blend original+dimmed by (255−lum)/255
            uint8_t out_r = ( rd * (255 - lum) + r * lum ) >> 8;
            uint8_t out_g = ( gd * (255 - lum) + g * lum ) >> 8;
            uint8_t out_b = ( bd * (255 - lum) + b * lum ) >> 8;

            // 5) repack into the pixel
            *row = (out_r << Rshift)
                 | (out_g << Gshift)
                 | (out_b << Bshift)
                 | (a     << Ashift);
        }
    }
}


// == 16-bit ARGB1555 ==
static inline void apply_scanlines(uint16_t *pixels,
                                   size_t width, size_t height,
                                   uint8_t shift,
                                   uint8_t Rshift, uint8_t Gshift, uint8_t Bshift, uint8_t Ashift,
                                   int     section)
{
    // Helper lambdas to scale between 5-bit and 8-bit without branches
    auto expand5  = [](uint32_t v5) -> uint32_t { return (v5 << 3) | (v5 >> 2); };                 // 0..31 -> 0..255
    auto quantize5 = [](uint32_t v8) -> uint32_t { return (v8 >> 3); };             // 0..255 -> 0..31

    const size_t block_height = (section >= 0 ? (height >> 1) : height);
    const size_t starty = (section == 1 ? block_height : 0);
    const size_t endy   = starty + block_height;

    const uint16_t Amask = (Ashift < 16) ? (uint16_t(1u) << Ashift) : 0; // A is 1 bit in 1555; 0 if no alpha in format

    for (size_t y = starty + 1; y < endy; y += 2) {
        uint16_t *row = pixels + y * width;
        for (size_t x = 0; x < width; ++x, ++row) {
            uint16_t p = *row;

            // Extract 5-bit channels
            const uint32_t r5 = (p >> Rshift) & 0x1Fu;
            const uint32_t g5 = (p >> Gshift) & 0x1Fu;
            const uint32_t b5 = (p >> Bshift) & 0x1Fu;
            const uint16_t a1 = (Ashift < 16) ? (p & Amask) : 0;

            // Upscale to 0..255
            const uint32_t r8 = expand5(r5);
            const uint32_t g8 = expand5(g5);
            const uint32_t b8 = expand5(b5);

            // Perceptual luminance (0..255)
            const uint32_t lum = (77u*r8 + 150u*g8 + 29u*b8) >> 8;

            // Dimmed versions (by 1 >> shift)
            const uint32_t rd8 = r8 >> shift;
            const uint32_t gd8 = g8 >> shift;
            const uint32_t bd8 = b8 >> shift;

            // Blend: darker where lum is low; preserve hue in bright areas
            const uint32_t out_r8 = ( rd8 * (255u - lum) + r8 * lum ) >> 8;
            const uint32_t out_g8 = ( gd8 * (255u - lum) + g8 * lum ) >> 8;
            const uint32_t out_b8 = ( bd8 * (255u - lum) + b8 * lum ) >> 8;

            // Quantize back to 5 bits
            const uint16_t out_r5 = (uint16_t)quantize5(out_r8);
            const uint16_t out_g5 = (uint16_t)quantize5(out_g8);
            const uint16_t out_b5 = (uint16_t)quantize5(out_b8);

            // Repack to ARGB1555 (R,G,B at their shifts; keep incoming A bit if present)
            *row = uint16_t((out_r5 << Rshift)
                          | (out_g5 << Gshift)
                          | (out_b5 << Bshift)
                          | a1);
        }
    }
}


static void apply_crt_bloom(uint32_t *pixels,
                            size_t   width,
                            size_t   height,
                            uint8_t  Rshift,
                            uint8_t  Gshift,
                            uint8_t  Bshift,
                            uint8_t  Ashift,
                            int      section)
{
    const size_t block_height = (section >= 0 ? (height >> 1) : height);
          size_t starty = (section == 1 ? block_height : 0);
    const size_t endy   = starty + block_height;

    // copy source so we don't pollute our reads
    int copy_rows   = (section >= 0 ? block_height+1 : block_height);
    int copy_start  = (section == 1 ? starty-1 : starty);
    size_t   n      = width * height;
    uint32_t *copy  = (uint32_t*)malloc(n * sizeof *copy);
    if (!copy) return;
    memcpy((copy + copy_start*width), (pixels + copy_start*width), width*copy_rows*sizeof *copy);

    for (size_t y = starty; y < endy; ++y) {
        // only do blur on the rows above/below scanlines:
        // those are the even indices when scanlines are at odd y
        if ((y % 2) == 0) {
            size_t y0 = (y == 0)        ? y : y - 1;
            size_t y1 = (y + 1 < height)? y + 1 : y;

            uint32_t *dst = pixels + y*width;
            uint32_t *row0 = copy + y0*width;
            uint32_t *row1 = copy + y1*width;
            uint32_t *orig = copy + y*width;

            for (size_t x = 0; x < width; ++x) {
                // unpack the three source pixels
                uint8_t r0 = (row0[x] >> Rshift) & 0xFF;
                uint8_t g0 = (row0[x] >> Gshift) & 0xFF;
                uint8_t b0 = (row0[x] >> Bshift) & 0xFF;

                uint8_t r1 = (row1[x] >> Rshift) & 0xFF;
                uint8_t g1 = (row1[x] >> Gshift) & 0xFF;
                uint8_t b1 = (row1[x] >> Bshift) & 0xFF;

                uint8_t ro = (orig[x] >> Rshift) & 0xFF;
                uint8_t go = (orig[x] >> Gshift) & 0xFF;
                uint8_t bo = (orig[x] >> Bshift) & 0xFF;
                uint8_t ao = (orig[x] >> Ashift) & 0xFF;

                // average them (1/3 each)
                uint8_t nr = (uint16_t(r0) + r1 + ro) / 3;
                uint8_t ng = (uint16_t(g0) + g1 + go) / 3;
                uint8_t nb = (uint16_t(b0) + b1 + bo) / 3;

                dst[x] = (nr << Rshift)
                       | (ng << Gshift)
                       | (nb << Bshift)
                       | (ao << Ashift);
            }
        }
    }
    free(copy);
}


int RenderSurface::get_blargg_config() {
    return (    config.video.blargg +
                config.video.saturation +
                config.video.contrast +
                config.video.brightness +
                config.video.sharpness +
                config.video.resolution +
                config.video.gamma +
                config.video.hue );
}


void RenderSurface::draw_frame(uint16_t* pixels, int fastpass)
{
    // grabs the S16 frame buffer ('pixels') and stores it, either
	// as straight SDL RGB or SNES RGB, then applies Blargg filter, if enabled, and colour mapping
    // fastpass (ignored if Blargg filter is not enabled):
    //   -1 = disabled; the whole frame is processed as well as the control values
    //    0 = enabled;  process the top half of the image with Blargg filter (if enabled) and control values
	//    1 = enabled;  only process the lower half of the image with Blargg filter (if enabled) then return

    if (config.videoRestartRequired) return;

    // check is disable() is waiting
    if (shutting_down.load(std::memory_order_acquire)) return;
    activity_counter.fetch_add(1, std::memory_order_acq_rel);

    // Snapshot the current write pointer under the lock (race-proof target for this call)
    void* current_writePixels;
    {
        std::lock_guard<std::mutex> lock(drawFrameMutex);
        current_writePixels = GameSurfacePixels;
    }

    if (blargg) {
        pixels = (uint16_t*)__builtin_assume_aligned(pixels, 4);
        uint32_t* writePixels = (uint32_t*)__builtin_assume_aligned(current_writePixels, 4);
        if (fastpass!=1) {
            if (config.fps == 60) phase = (phase + 1) % 3; // cycle through 0/1/2
            else                  phase = (phase + 2) % 3; // cycle through 0/1/2, but at twice the rate
        }
        blargg_filter(pixels, writePixels, fastpass);
        // apply scanlines, if enabled.
        if (config.video.scanlines!=0) {
            apply_scanlines(writePixels, snes_src_width, src_height, config.video.scanlines,
                            Rshift, Gshift, Bshift, Ashift, fastpass);
//            apply_crt_bloom(writePixels, snes_src_width, src_height,
//                            Rshift, Gshift, Bshift, Ashift, fastpass);
        }
    } else if (fastpass!=1) {
        // Standard image processing; direct RGB value lookup from rgb array for backbuffer
        // Single-threaded only for standard path
        size_t pixel_count = src_width * src_height;
        uint32_t Ashifted = uint32_t(Alevel) << Ashift;

        // translate game image to S16-correct RGB output levels
        pixels = (uint16_t*)__builtin_assume_aligned(pixels, 4);
        uint16_t* writePixels = (uint16_t*)__builtin_assume_aligned(current_writePixels, 4);
        uint16_t* spix = pixels;
        uint16_t* tpix = writePixels;
        for (size_t i = 0; i < pixel_count; i+=4) {
            tpix[i+0] = s16_rgb555[spix[i+0]];
            tpix[i+1] = s16_rgb555[spix[i+1]];
            tpix[i+2] = s16_rgb555[spix[i+2]];
            tpix[i+3] = s16_rgb555[spix[i+3]];
        }

        // apply scanlines, if enabled (full frame single-thread)
        if (config.video.scanlines!=0) {
            writePixels = tpix; // reset
            apply_scanlines(writePixels, src_width, src_height, config.video.scanlines,
                            1,6,11,0,-1);
//                            Rshift, Gshift, Bshift, Ashift, -1);
//            apply_crt_bloom(writePixels, src_width, src_height,
//                            Rshift, Gshift, Bshift, Ashift, -1);
        }
    }

    if ((fastpass!=1) && (!config.videoRestartRequired)) {
        // Check for any changes to the configured video settings (that don't require full SDL restart)
        // Blargg filter settings. Changing these requires Blargg filter re-initialisation.
        int this_blargg_config = get_blargg_config();
        if (this_blargg_config != last_blargg_config) {
            // Settings have been changed; capture new setting & re-initiatise the Blargg filter library
            // The filter uses doubles internally, so we need to convert the config values to doubles.
            last_blargg_config =  this_blargg_config;
            blargg             =  config.video.blargg;
            setup.saturation   =  double(config.video.saturation) / 100;
            setup.contrast     =  double(config.video.contrast) / 100;
            setup.brightness   =  double(config.video.brightness) / 100;
            setup.sharpness    =  double(config.video.sharpness) / 100;
            setup.resolution   =  double(config.video.resolution) / 100;
            setup.gamma        =  double(config.video.gamma) / 10;
            setup.hue          =  double(config.video.hue) / 100;

            // pause video whilst this is re-calculated
            drawFrameMutex.lock();
            //destroy_buffers();
            init_blargg_filter();
            //create_buffers();
            drawFrameMutex.unlock();
        }
    }

    // notify disable() that we're done
    activity_counter.fetch_sub(1, std::memory_order_acq_rel);
    std::unique_lock<std::mutex> lock(mtx);
    cv.notify_all();  // In case disable() is waiting
}
