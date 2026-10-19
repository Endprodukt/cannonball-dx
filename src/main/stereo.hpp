/***************************************************************************
    Stereoscopic 3D ("layered stereo") support for CannonBall DX.

    OutRun is a 2D sprite/road-layer game, but every layer carries enough
    information to place it at a believable depth:

      - Sprites: the engine's zoom index (0..255). Apparent size, and
        therefore inverse depth, is proportional to (47 * zoom + 111), with
        zoom 0x7F being the player's Ferrari.
      - Road: every scanline selects a pre-rendered road ROM row. The width
        of the road in that row is proportional to inverse depth, whatever
        the hills are doing.
      - Tilemaps: distant backdrop, treated as (near) infinity.
      - Text / HUD: kept on the screen plane.

    Each frame is rendered twice, once per eye, with every element shifted
    horizontally by half of its stereo separation. The two eye images are then
    packed into one frame (half side-by-side etc.) so that every existing
    renderer, scaler and filter keeps working unchanged.

    Depth convention: "inv" is inverse depth relative to the Ferrari.
      inv = 1   -> screen plane (the car, the HUD)
      inv = 0   -> infinity (separation = stereo_depth)
      inv > 1   -> in front of the screen (pop-out, limited by stereo_popout)
***************************************************************************/

#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

namespace stereo
{
    // Output packing modes (config: video.stereo)
    enum Mode : int
    {
        MODE_OFF       = 0, // normal 2D
        MODE_SBS       = 1, // half side-by-side, left eye on the left (3D TVs, VR SBS viewers)
        MODE_SBS_CROSS = 2, // half side-by-side, eyes swapped (cross-eye free viewing)
        MODE_TAB       = 3, // half top-and-bottom, left eye on top
        MODE_COUNT
    };

    constexpr int EYE_NONE  = 0;
    constexpr int EYE_LEFT  = -1;
    constexpr int EYE_RIGHT = 1;

    // Inverse depth of the Ferrari (zoom 0x7F), used to normalise sprites
    // that have no world depth (smoke, crash debris, clouds, logos).
    constexpr float FERRARI_ZOOM_SIZE = 47.0f * 0x7F + 111.0f;

    // World depth of road objects (scenery and traffic): the engine's z16
    // (4..0x1FF, larger = closer). The zoom index cannot be used for these:
    // each object family derives it differently (scenery z16 >> shift with a
    // per-object shift, traffic (z16 >> 2) scaled by vehicle size, ground
    // strips such as grass and water pin it at 0x80 and switch artwork
    // instead). z16 is the same scale for all of them.
    //
    // Traffic placed level with the player on the start grid sits at 0x1D0,
    // so that is the car's plane. Apparent size, and so inverse depth, of
    // scenery is proportional to 47 * (z16 / 2) + 111, i.e. z16 + 4.72.
    constexpr int   CAR_PLANE_Z   = 0x1D0;
    constexpr float WORLD_Z_BIAS  = 111.0f / 23.5f;

    inline float world_inv(int z16)
    {
        return (z16 + WORLD_Z_BIAS) / (CAR_PLANE_Z + WORLD_Z_BIAS);
    }

    // Depth tag stored in word 8 of each hardware sprite entry:
    //   bit 15     valid
    //   bit 14     1 = world depth (bits 0-8 = z16), 0 = zoom index (bits 0-7)
    constexpr uint16_t TAG_VALID = 0x8000;
    constexpr uint16_t TAG_WORLD = 0x4000;

    inline uint16_t tag_world(int z16) { return static_cast<uint16_t>(TAG_VALID | TAG_WORLD | (z16 & 0x1FF)); }
    inline uint16_t tag_zoom(uint8_t zoom) { return static_cast<uint16_t>(TAG_VALID | zoom); }

    // Fixed layer depths
    constexpr float INV_TILES_BACK  = 0.00f; // far backdrop (page 1)
    constexpr float INV_TILES_FRONT = 0.03f; // nearer hills / skyline (page 0)
    constexpr float INV_SCREEN      = 1.00f; // HUD, text, menus

    // Per-frame state shared by the layer renderers.
    struct State
    {
        int   eye         = EYE_NONE; // eye currently being rendered
        float separation  = 0.0f;     // total separation at infinity, logical pixels
        float inv_max     = 1.5f;     // pop-out limit
        bool  road_active = false;    // true when the road layer is on screen
    };
    extern State state;

    // Calibration samples recorded by the sprite engine during the game tick:
    // ground-anchored sprites standing on the road (bottom scanline + inverse depth).
    struct Samples
    {
        static const int MAX = 64;
        int     count = 0;
        int16_t y[MAX];
        float   inv[MAX];
    };
    extern Samples samples;

    // True when a stereo output mode is selected in the config.
    bool enabled();

    // Call once per frame before rendering any eye (reads config).
    void begin_frame();

    // Record a calibration sample (from OSprites::do_sprite).
    inline void add_sample(int y_bottom, float inv)
    {
        if (samples.count < Samples::MAX)
        {
            samples.y[samples.count]   = static_cast<int16_t>(y_bottom);
            samples.inv[samples.count] = inv;
            samples.count++;
        }
    }

    // Inverse depth of a sprite from its engine zoom index (no world depth).
    inline float sprite_inv(uint8_t zoom)
    {
        return (47.0f * zoom + 111.0f) / FERRARI_ZOOM_SIZE;
    }

    // Inverse depth from a sprite's depth tag (see TAG_*).
    inline float tag_inv(uint16_t tag)
    {
        return (tag & TAG_WORLD) ? world_inv(tag & 0x1FF) : sprite_inv(static_cast<uint8_t>(tag & 0xFF));
    }

    // Horizontal shift for the current eye, in logical (native 320px-era) pixels.
    inline float shift_logical(float inv)
    {
        if (state.eye == EYE_NONE)
            return 0.0f;
        inv = std::clamp(inv, 0.0f, state.inv_max);
        return state.eye * 0.5f * state.separation * (1.0f - inv);
    }

    // Shift in render-target pixels (hi-res modes get sub-native precision).
    inline int shift_render(float inv, int render_scale)
    {
        return static_cast<int>(std::lround(shift_logical(inv) * render_scale));
    }

    // Shift for a tilemap page (logical pixels, integer).
    inline int tile_shift(int page_index)
    {
        if (state.eye == EYE_NONE || !state.road_active)
            return 0;
        const float inv = (page_index == 0) ? INV_TILES_FRONT : INV_TILES_BACK;
        return static_cast<int>(std::lround(shift_logical(inv)));
    }

    // Pack the left- and right-eye images into the single frame 'out'
    // according to 'mode'. All three buffers are width x height.
    void compose(uint16_t* out, const uint16_t* left, const uint16_t* right,
                 int width, int height, int mode);

    // Human-readable name of a mode, for logging.
    const char* mode_name(int mode);

    // Cycle helpers for hotkeys (forward, wrapping).
    void cycle_mode();
    void cycle_depth();

    // Step a setting forwards (+1) or backwards (-1), wrapping.
    void step_mode(int direction);
    void step_depth(int direction);
    void step_popout(int direction);

    // Frontend menu rows (VIDEO > VIDEO ENHANCEMENTS). Text uses only
    // characters available in the OutRun font.
    constexpr const char* MENU_MODE_LABEL   = "STEREO 3D ";
    constexpr const char* MENU_DEPTH_LABEL  = "3D DEPTH ";
    constexpr const char* MENU_POPOUT_LABEL = "3D POP OUT ";
    std::string menu_mode_text();
    std::string menu_depth_text();
    std::string menu_popout_text();
}
