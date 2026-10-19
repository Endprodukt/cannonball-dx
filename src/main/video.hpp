/***************************************************************************
    Video Rendering. 
    
    - Renders the System 16 Video Layers
    - Handles Reads and Writes to these layers from the main game code
    - Interfaces with platform specific rendering code

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include "stdint.hpp"
#include "globals.hpp"
#include "roms.hpp"
#include "hwvideo/hwtiles.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwroad.hpp"

namespace shadow
{
    const static float ORIGINAL = 0.63f; // Hardware Intensity (63%)
    const static float MAME = 0.78f;     // Mame Intensity (78%)
};

class hwsprites;
class RenderBase;
struct video_settings_t;

class Video
{
public:
	hwsprites* sprite_layer;
    hwtiles* tile_layer;
    uint16_t* pixel_buffers[2] = {nullptr, nullptr};
    uint16_t* pixels;
    int current_pixel_buffer = 0;

    bool enabled;

	Video();
    ~Video();
    
	int init(Roms* roms, video_settings_t* settings, bool preserve_hardware_state = false);
    void swap_buffers();
    void disable();
    void focus_window();
    int set_video_mode(video_settings_t* settings);
    void set_shadow_intensity(float);
    void prepare_frame();
    void render_frame(int fastpass);
    void present_frame();
    bool supports_window();
    bool supports_vsync();

    // Stereo 3D per-eye render target (0 = left, 1 = right), or nullptr.
    // Code that patches the framebuffer directly between frames must patch
    // these too, as they persist frame to frame like 'pixels' does.
    uint16_t* stereo_eye_pixels(int eye) const { return (eye == 0 || eye == 1) ? stereo_pixels[eye] : nullptr; }

    void clear_text_ram();
    void write_text8(uint32_t, const uint8_t);
	void write_text16(uint32_t*, const uint16_t);
	void write_text16(uint32_t, const uint16_t);
    void write_text32(uint32_t*, const uint32_t);
    void write_text32(uint32_t, const uint32_t);
    uint8_t read_text8(uint32_t);

    void clear_tile_ram();
    void write_tile8(uint32_t, const uint8_t);
	void write_tile16(uint32_t*, const uint16_t);
	void write_tile16(uint32_t, const uint16_t);
    void write_tile32(uint32_t*, const uint32_t);
    void write_tile32(uint32_t, const uint32_t);
    uint8_t read_tile8(uint32_t);

	void write_sprite16(uint32_t*, const uint16_t);

	void write_pal8(uint32_t*, const uint8_t);
	void write_pal16(uint32_t*, const uint16_t);
	void write_pal32(uint32_t*, const uint32_t);
	void write_pal32(uint32_t, const uint32_t);
	uint8_t read_pal8(uint32_t);
	uint16_t read_pal16(uint32_t*);
	uint16_t read_pal16(uint32_t);
    uint32_t read_pal32(uint32_t*);

private:
    // SDL Renderer
    RenderBase* renderer;

    const int alignment = 64;
	alignas(64) uint8_t palette[S16_PALETTE_ENTRIES * 2]; // 2 Bytes Per Palette Entry
    void refresh_palette(uint32_t);

    // Stereo 3D: persistent per-eye render targets (same layout and padding
    // as pixel_buffers). Each eye keeps its own previous frame, exactly like
    // the single 2D target, then both are packed into 'pixels'.
    uint16_t* stereo_buffers[2] = {nullptr, nullptr};
    uint16_t* stereo_pixels[2]  = {nullptr, nullptr}; // [0] = left, [1] = right
    void render_layers(uint16_t* target, bool advance_animation);

};

extern Video video;
