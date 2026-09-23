/**********************************************************************************
    SDL2 Video Rendering
    Original SDL works Copyright (c) 2012,2020 Manuel Alfayate, Chris White.

    See license.txt for more details.

    This version, for CannnonBall SE, Copyright (c) 2020,2025 James Pearce.

    Provides:
    - Internal format palette conversion for Blargg filter (eliminates one step)
    - ARGB 1555 format for unfiltered output (halving image size)

***********************************************************************************/

#include "renderbase.hpp"
#include <iostream>


RenderBase::RenderBase()
{
    surface       = NULL;
    screen_pixels = NULL;

    orig_width  = 0;
    orig_height = 0;

    // initial colour lookup tables
    memset(s16_rgb555, 0, S16_PALETTE_ENTRIES * 2 * sizeof(uint16_t));
    memset(rgb_blargg, 0, S16_PALETTE_ENTRIES * 2 * sizeof(uint16_t));
}

// Setup screen size
bool RenderBase::sdl_screen_size()
{
    if (orig_width == 0 || orig_height == 0)
    {
	SDL_DisplayMode info;

	SDL_GetCurrentDisplayMode(0, &info);

        orig_width  = info.w;
        orig_height = info.h;
    }

    scn_width  = orig_width;
    scn_height = orig_height;

    return true;
}


// The following lookup tables match the ladder DAC to linear RGB values, assuming the arcade monitor
// has an input impedence of 2k2. Shadows and highlights work by applying an extra 220R resistor to
// VCC or GND when called, drived by a 74LS125 (tri-state).
//
// These are used to convert the S16 tile colours to RGB values for rendering, via applied colour curve
// settings which applied.

const uint32_t S16_rgbVal[32] = {
  0,8,16,24,31,39,47,55,62,70,78,86,94,102,109,117,125,133,140,148,156,164,171,179,187,195,203,211,
  218,226,234,242};

const uint32_t S16_rgbVal_5bit[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30};

const uint32_t S16_shadowVal[32] = {
  0,5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100,105,110,115,120,126,130,136,140,146,
  150,156};

const uint32_t S16_shadowVal_5bit[32] = {
    0, 1, 1, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 11, 12, 13, 13, 14, 15, 16, 16, 17, 18,
    18, 19, 20};

const uint32_t S16_hiliteVal[32] = {
  91,96,101,106,111,116,121,126,131,137,141,147,151,157,161,167,172,177,182,187,192,197,202,207,212,
  217,222,227,232,237,242,247};


// See: SDL_PixelFormat
//#define CURRENT_RGB() (r << Rshift) | (g << Gshift) | (b << Bshift) | (255 << Ashift);
// Blargg filter expects 565 format
//#define CURRENT_RGB_BLARRG() ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);

// The following passes the S16 raw RGB values plus flags for shadow/hilite to Blargg
// The actual S16 output values are looked up in the Blargg filter
// This reduces the computation required for the Blargg filter to convert the S16 values to RGB
// to 32 x 32 x 32 x 2 = 65,536 calculations at initialistion (otherwise would be 256x256x256=16,777,216)
#define S16_STANDARD 0x0000
#define S16_SHADOW   0x8000
#define CURRENT_RGB_BLARRG_STD()    (r1 << 10) | (g1 << 5) | b1 | S16_STANDARD;
#define CURRENT_RGB_BLARRG_SHADOW() (r1 << 10) | (g1 << 5) | b1 | S16_SHADOW;
#define RGB1555_ALPHA 0x0001

void RenderBase::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;

    // Standard colours
    s16_rgb555[adr] = (S16_rgbVal_5bit[r1] << 11)  |
                      (S16_rgbVal_5bit[g1] << 6)  |
                      (S16_rgbVal_5bit[b1] << 1) |
                      RGB1555_ALPHA;
	rgb_blargg[adr]  = CURRENT_RGB_BLARRG_STD();

    // Shadows
    s16_rgb555[adr + S16_PALETTE_ENTRIES] =
                     (S16_shadowVal_5bit[r1] << 1)  |
                     (S16_shadowVal_5bit[g1] << 6)  |
                     (S16_shadowVal_5bit[b1] << 11) |
                     RGB1555_ALPHA;
    rgb_blargg[adr + S16_PALETTE_ENTRIES] = CURRENT_RGB_BLARRG_SHADOW();
}


void RenderBase::set_shadow_intensity(float f)
{
    shadow_multi = (int) std::round(255.0f * f);
}

void RenderBase::init_palette(int red_curve, int green_curve, int blue_curve)
{
    return;
}
