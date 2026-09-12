#include <algorithm> // Required for std::fill_n
#include "hwvideo/hwroad.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"

/***************************************************************************
    Video Emulation: OutRun Road Rendering Hardware.
    Based on MAME source code.

    Copyright Aaron Giles.
    Performance optimisations Copyright (c) 2025, James Pearce
    All rights reserved.

    This version for CannonBall-SE incorporates revisions Copyright (c)
    2025 James Pearce:
    - Performance tuning
***************************************************************************/

/*******************************************************************************************
 *
 *  Out Run/X-Board-style road chip
 *
 *  Road control register:
 *      Bits               Usage
 *      -------- -----d--  (X-board only) Direct scanline mode (1) or indirect mode (0)
 *      -------- ------pp  Road enable/priorities:
 *                            0 = road 0 only visible
 *                            1 = both roads visible, road 0 has priority
 *                            2 = both roads visible, road 1 has priority
 *                            3 = road 1 only visible
 *
 *  Road RAM:
 *      Offset   Bits               Usage
 *      000-1FF  ----s--- --------  Road 0: Solid fill (1) or ROM fill
 *               -------- -ccccccc  Road 0: Solid color (if solid fill)
 *               -------i iiiiiiii  Road 0: Index for other tables (if in indirect mode)
 *               -------r rrrrrrr-  Road 0: Road ROM line select
 *      200-3FF  ----s--- --------  Road 1: Solid fill (1) or ROM fill
 *               -------- -ccccccc  Road 1: Solid color (if solid fill)
 *               -------i iiiiiiii  Road 1: Index for other tables (if in indirect mode)
 *               -------r rrrrrrr-  Road 1: Road ROM line select
 *      400-7FF  ----hhhh hhhhhhhh  Road 0: horizontal scroll
 *      800-BFF  ----hhhh hhhhhhhh  Road 1: horizontal scroll
 *      C00-FFF  ----bbbb --------  Background color index
 *               -------- s-------  Road 1: stripe color index
 *               -------- -a------  Road 1: pixel value 2 color index
 *               -------- --b-----  Road 1: pixel value 1 color index
 *               -------- ---c----  Road 1: pixel value 0 color index
 *               -------- ----s---  Road 0: stripe color index
 *               -------- -----a--  Road 0: pixel value 2 color index
 *               -------- ------b-  Road 0: pixel value 1 color index
 *               -------- -------c  Road 0: pixel value 0 color index
 *
 *  Logic:
 *      First, the scanline is used to index into the tables at 000-1FF/200-3FF
 *          - if solid fill, the background is filled with the specified color index
 *          - otherwise, the remaining tables are used
 *
 *      If indirect mode is selected, the index is taken from the low 9 bits of the
 *          table value from 000-1FF/200-3FF
 *      If direct scanline mode is selected, the index is set equal to the scanline
 *          for road 0, or the scanline + 256 for road 1
 *
 *      The horizontal scroll value is looked up using the index in the tables at
 *          400-7FF/800-BFF
 *
 *      The color information is looked up using the index in the table at C00-FFF. Note
 *          that the same table is used for both roads.
 *
 *
 *  Out Run road priorities are controlled by a PAL that maps as indicated below.
 *  This was used to generate the priority_map. It is assumed that X-board is the
 *  same, though this logic is locked inside a Sega custom.
 *
 *  RRC0 =  CENTA & (RDA == 3) & !RRC2
 *      | CENTB & (RDB == 3) & RRC2
 *      | (RDA == 1) & !RRC2
 *      | (RDB == 1) & RRC2
 *
 *  RRC1 =  CENTA & (RDA == 3) & !RRC2
 *      | CENTB & (RDB == 3) & RRC2
 *      | (RDA == 2) & !RRC2
 *      | (RDB == 2) & RRC2
 *
 *  RRC2 = !/HSYNC & IIQ
 *      | (CTRL == 3)
 *      | !CENTA & (RDA == 3) & !CENTB & (RDB == 3) & (CTRL == 2)
 *      | CENTB & (RDB == 3) & (CTRL == 2)
 *      | !CENTA & (RDA == 3) & !M2 & (CTRL == 2)
 *      | !CENTA & (RDA == 3) & !M3 & (CTRL == 2)
 *      | !M0 & (RDB == 0) & (CTRL == 2)
 *      | !M1 & (RDB == 0) & (CTRL == 2)
 *      | !CENTA & (RDA == 3) & CENTB & (RDB == 3) & (CTRL == 1)
 *      | !M0 & CENTB & (RDB == 3) & (CTRL == 1)
 *      | !M1 & CENTB & (RDB == 3) & (CTRL == 1)
 *      | !CENTA & M0 & (RDB == 0) & (CTRL == 1)
 *      | !CENTA & M1 & (RDB == 0) & (CTRL == 1)
 *      | !CENTA & (RDA == 3) & (RDB == 1) & (CTRL == 1)
 *      | !CENTA & (RDA == 3) & (RDB == 2) & (CTRL == 1)
 *
 *  RRC3 =  VA11 & VB11
 *      | VA11 & (CTRL == 0)
 *      | (CTRL == 3) & VB11
 *
 *  RRC4 =  !CENTA & (RDA == 3) & !CENTB & (RDB == 3)
 *      | VA11 & VB11
 *      | VA11 & (CTRL == 0)
 *      | (CTRL == 3) & VB11
 *      | !CENTB & (RDB == 3) & (CTRL == 3)
 *      | !CENTA & (RDA == 3) & (CTRL == 0)
 *
 *******************************************************************************************/

HWRoad hwroad;

HWRoad::HWRoad()
{
}

HWRoad::~HWRoad()
{
}

// Convert road to a more useable format
void HWRoad::init(const uint8_t* src_road, const bool hires)
{
    // A nullptr source is a renderer-only restart. Preserve the live road
    // registers/state and only refresh the lores/hires render entry points.
    if (src_road)
    {
        road_control = 0;
        color_offset1 = 0x400;
        color_offset2 = 0x420;
        color_offset3 = 0x780;
        x_offset = 0;
        decode_road(src_road);
    }

    if (hires)
    {
        render_background = &HWRoad::render_background_hires;
        render_foreground = &HWRoad::render_foreground_hires;
    }
    else
    {
        render_background = &HWRoad::render_background_lores;
        render_foreground = &HWRoad::render_foreground_lores;
    }
}

/*
    There are TWO (identical) roads we need to decode.
    Each of these roads is represented using a 512x256 map.
    See: http://www.extentofthejam.com/pseudo/
      
    512 x 256 x 2bpp map 
    0x8000 bytes of data. 
    2 Bits Per Pixel.
       
    Per Road:
    Bit 0 of each pixel is stored at offset 0x0000 - 0x3FFF
    Bit 1 of each pixel is stored at offset 0x4000 - 0x7FFF

    This means: 80 bytes per X Row [2 x 0x40 Bytes from the two separate locations]

    Decoded Format:  
    0 = Road Colour
    1 = Road Inner Stripe
    2 = Road Outer Stripe
    3 = Road Exterior
    7 = Central Stripe
*/

void HWRoad::decode_road(const uint8_t* src_road)
{
    for (int y = 0; y < 256 * 2; y++) 
    {
        const int src = ((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % rom_size; // tempGfx
        const int dst = y * 512; // System16Roads

        // loop over columns
        for (int x = 0; x < 512; x++) 
        {
            roads[dst + x] = (((src_road[src + (x / 8)] >> (~x & 7)) & 1) << 0) | (((src_road[src + (x / 8 + 0x4000)] >> (~x & 7)) & 1) << 1);

            // pre-mark road data in the "stripe" area with a high bit
            if (x >= 256 - 8 && x < 256 && roads[dst + x] == 3)
                roads[dst + x] |= 4;
        }
    }

    // set up a dummy road in the last entry
    for (int i = 0; i < 512; i++) 
    {
        roads[256 * 2 * 512 + i] = 3;
    }
}

// Writes go to RAM, but we read from the RAM Buffer.
/* JJP - moved to header, inline
void HWRoad::write16(uint32_t adr, const uint16_t data)
{
    ram[(adr >> 1) & 0x7FF] = data;
}

void HWRoad::write16(uint32_t* adr, const uint16_t data)
{
    uint32_t a = *adr;
    ram[(a >> 1) & 0x7FF] = data;
    *adr += 2;
}

void HWRoad::write32(uint32_t* adr, const uint32_t data)
{
    uint32_t a = *adr;
    ram[(a >> 1) & 0x7FF] = data >> 16;
    ram[((a >> 1) + 1) & 0x7FF] = data & 0xFFFF;
    *adr += 4;
}
*/
uint16_t HWRoad::read_road_control()
{
    uint32_t *src = (uint32_t *)ram;
    uint32_t *dst = (uint32_t *)ramBuff;

    // swap the halves of the road RAM
    for (uint16_t i = 0; i < ROAD_RAM_SIZE/4; i++)
    {
        uint32_t temp = *src;
        *src++ = *dst;
        *dst++ = temp;
    }

    return 0xffff;
}

void HWRoad::write_road_control(const uint8_t road_control)
{
    this->road_control = road_control;
}

// ------------------------------------------------------------------------------------------------
// Road Rendering: Lores Version
// ------------------------------------------------------------------------------------------------

// Background: Look for solid fill scanlines
void HWRoad::render_background_lores(uint16_t* pixels)
{
    const uint16_t* roadram = ramBuff;

    for (uint16_t y = 0; y < S16_HEIGHT; y++)
    {
        const int data0 = roadram[0x000 + y];
        const int data1 = roadram[0x100 + y];

        int color = -1;

        // based on the info->control, we can figure out which sky to draw
        switch (road_control & 3)
        {
            case 0:
                if (data0 & 0x800)
                    color = data0 & 0x7f;
                break;

            case 1:
                if (data0 & 0x800)
                    color = data0 & 0x7f;
                else if (data1 & 0x800)
                    color = data1 & 0x7f;
                break;

            case 2:
                if (data1 & 0x800)
                    color = data1 & 0x7f;
                else if (data0 & 0x800)
                    color = data0 & 0x7f;
                break;

            case 3:
                if (data1 & 0x800)
                    color = data1 & 0x7f;
                break;
        }

        if (color != -1) {
            const std::size_t w = config.s16_width;
            uint16_t c = static_cast<uint16_t>(color | color_offset3);
            uint16_t* pPixel = pixels + (y * w);
            uint32_t* out32 = reinterpret_cast<uint32_t*>(pPixel); // enable writing as 32-bit values
            // Fill both y and y+1 scanlines with final_color
            // Total pixels to fill: width * 2 pixels = 2 lines
            std::fill_n(out32, w >> 1, static_cast<uint32_t>(c << 16) | c);
        }
    }
}


// Foreground: Render From ROM
void HWRoad::render_foreground_lores(uint16_t* pixels)
{
    uint16_t* roadram = ramBuff;

    for (uint16_t y = 0; y < S16_HEIGHT; y++)
    {
        uint16_t color_table[32];

        static const uint8_t priority_map[2][8] =
        {
            { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
            { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
        };
        // Define a lookup table mapping (pix0, pix1) to color indices
        // for the hot path
        static const ALIGN64 uint8_t priority_lookup[8][8] = {
            // pix1: 0  1  2  3  4  5  6  7
            {   0,  0,  0,  0,  0,  0,  0, 1 }, // pix0 = 0
            {   1,  0,  0,  0,  0,  0,  0, 1 }, // pix0 = 1
            {   1,  0,  0,  0,  0,  0,  0, 1 }, // pix0 = 2
            {   1,  1,  1,  0,  0,  0,  0, 1 }, // pix0 = 3
            {   0,  0,  0,  0,  0,  0,  0, 0 }, // pix0 = 4
            {   0,  0,  0,  0,  0,  0,  0, 0 }, // pix0 = 5
            {   0,  0,  0,  0,  0,  0,  0, 0 }, // pix0 = 6
            {   0,  0,  0,  0,  0,  0,  0, 0 }  // pix0 = 7
        };

        const uint32_t data0 = roadram[0x000 + y];
        const uint32_t data1 = roadram[0x100 + y];

        // if both roads are low priority, skip
        if (((data0 & 0x800) != 0) && ((data1 & 0x800) != 0))
            continue;

        uint16_t* pPixel = pixels + (y * config.s16_width);
        uint32_t hpos0, hpos1, color0, color1;
        uint32_t control = road_control & 3;

        uint8_t *src0, *src1;
        uint32_t bgcolor; // 8 bits

        // get road 0 data
        src0   = ((data0 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x000 + ((data0 >> 1) & 0xff)) * 512);
        hpos0  = roadram[0x200 + (((road_control & 4) != 0) ? y : (data0 & 0x1ff))] & 0xfff;
        color0 = roadram[0x600 + (((road_control & 4) != 0) ? y : (data0 & 0x1ff))];

        // get road 1 data
        src1   = ((data1 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x100 + ((data1 >> 1) & 0xff)) * 512);
        hpos1  = roadram[0x400 + (((road_control & 4) != 0) ? (0x100 + y) : (data1 & 0x1ff))] & 0xfff;
        color1 = roadram[0x600 + (((road_control & 4) != 0) ? (0x100 + y) : (data1 & 0x1ff))];

        // determine the 5 colors for road 0
        color_table[0x00] = color_offset1 ^ 0x00 ^ ((color0 >> 0) & 1);
        color_table[0x01] = color_offset1 ^ 0x02 ^ ((color0 >> 1) & 1);
        color_table[0x02] = color_offset1 ^ 0x04 ^ ((color0 >> 2) & 1);
        bgcolor = (color0 >> 8) & 0xf;
        color_table[0x03] = ((data0 & 0x200) != 0) ? color_table[0x00] : (color_offset2 ^ 0x00 ^ bgcolor);
        color_table[0x07] = color_offset1 ^ 0x06 ^ ((color0 >> 3) & 1);

        // determine the 5 colors for road 1
        color_table[0x10] = color_offset1 ^ 0x08 ^ ((color1 >> 4) & 1);
        color_table[0x11] = color_offset1 ^ 0x0a ^ ((color1 >> 5) & 1);
        color_table[0x12] = color_offset1 ^ 0x0c ^ ((color1 >> 6) & 1);
        bgcolor = (color1 >> 8) & 0xf;
        color_table[0x13] = ((data1 & 0x200) != 0) ? color_table[0x10] : (color_offset2 ^ 0x10 ^ bgcolor);
        color_table[0x17] = color_offset1 ^ 0x0e ^ ((color1 >> 7) & 1);

        // Shift road dependent on whether we are in widescreen mode or not
        uint16_t s16_x = 0x5f8 + config.s16_x_off;

        // draw the road
        switch (control)
        {
            case 0: {
                if (data0 & 0x800)
                    continue;
                uint16_t pairs = config.s16_width >> 1;
                uint32_t h0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                uint32_t* out32 = reinterpret_cast<uint32_t*>(pPixel);
                while (pairs--) {
                    // pixel 0
                    unsigned p0 = 3u;
                    if (h0 < 0x200u) p0 = src0[h0];
                    uint16_t c0 = color_table[p0];
                    h0 = (h0 + 1) & 0xfff;

                    // pixel 1
                    p0 = 3u;
                    if (h0 < 0x200u) p0 = src0[h0];
                    uint16_t c1 = color_table[p0];
                    h0 = (h0 + 1) & 0xfff;

                    // one 32-bit store (c0 low, c1 high; little-endian)
                    *out32++ = (uint32_t)c0 | ((uint32_t)c1 << 16);
                }
                hpos0 = h0;
                break;
            }

            case 1: {
                // hot path
                uint16_t pairs = config.s16_width >> 1;               // number of 2-pixel pairs
                uint32_t h0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                uint32_t h1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                uint32_t* out32 = reinterpret_cast<uint32_t*>(pPixel);

                while (pairs--) {
                    unsigned p0 = 3u;
                    unsigned p1 = 3u;
                    if (h0 < 0x200u) p0 = src0[h0];
                    if (h1 < 0x200u) p1 = src1[h1];

                    uint16_t c0;
                    if (priority_lookup[p0][p1]) {
                        c0 = color_table[0x10 + p1];
                    } else {
                        // this is the hot path, 85% or so, rely on branch prediction to handle it
                        c0 = color_table[0x00 + p0];
                    }

                    h0 = (h0 + 1) & 0xfff;
                    h1 = (h1 + 1) & 0xfff;

                    p0 = 3u;
                    p1 = 3u;
                    if (h0 < 0x200u) p0 = src0[h0];
                    if (h1 < 0x200u) p1 = src1[h1];

                    uint16_t c1;
                    if (priority_lookup[p0][p1]) {
                        c1 = color_table[0x10 + p1];
                    } else {
                        // this is the hot path, 85% or so, rely on branch prediction to handle it
                        c1 = color_table[0x00 + p0];
                    }

                    h0 = (h0 + 1) & 0xfff;
                    h1 = (h1 + 1) & 0xfff;

                    *out32++ = (uint32_t)c0 | ((uint32_t)c1 << 16);
                }
                hpos0 = h0;
                hpos1 = h1;

                break;
            }

            case 2: {
                uint16_t pairs = config.s16_width >> 1;               // number of 2-pixel pairs
                uint32_t h0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                uint32_t h1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                uint32_t* out32 = reinterpret_cast<uint32_t*>(pPixel);

                while (pairs--) {
                    unsigned p0 = 3u;
                    unsigned p1 = 3u;
                    if (h0 < 0x200u) p0 = src0[h0];
                    if (h1 < 0x200u) p1 = src1[h1];

                    uint16_t c0;
                    if (((priority_map[1][p0] >> p1) & 1) != 0) {
                        c0 = color_table[0x10 + p1];
                    } else {
                        c0 = color_table[0x00 + p0];
                    }

                    h0 = (h0 + 1) & 0xfff;
                    h1 = (h1 + 1) & 0xfff;

                    p0 = 3u;
                    p1 = 3u;
                    if (h0 < 0x200u) p0 = src0[h0];
                    if (h1 < 0x200u) p1 = src1[h1];

                    uint16_t c1;
                    if (((priority_map[1][p0] >> p1) & 1) != 0) {
                        c1 = color_table[0x10 + p1];
                    } else {
                        c1 = color_table[0x00 + p0];
                    }

                    h0 = (h0 + 1) & 0xfff;
                    h1 = (h1 + 1) & 0xfff;

                    *out32++ = (uint32_t)c0 | ((uint32_t)c1 << 16);
                }
                hpos0 = h0;
                hpos1 = h1;

                break;
            }

            case 3: {
                if (data1 & 0x800)
                    continue;
                uint16_t pairs = config.s16_width >> 1;
                uint32_t h1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                uint32_t* out32 = reinterpret_cast<uint32_t*>(pPixel);
                while (pairs--) {
                    // pixel 0
                    unsigned p0 = 3u;
                    if (h1 < 0x200u) p0 = src1[h1];
                    uint16_t c0 = color_table[0x10 + p0];
                    h1 = (h1 + 1) & 0xfff;

                    // pixel 1
                    p0 = 3u;
                    if (h1 < 0x200u) p0 = src1[h1];
                    uint16_t c1 = color_table[0x10 + p0];
                    h1 = (h1 + 1) & 0xfff;

                    // one 32-bit store (c0 low, c1 high; little-endian)
                    *out32++ = (uint32_t)c0 | ((uint32_t)c1 << 16);
                }
                hpos1 = h1;
                break;
            }
        } // end switch
    } // end for
}

// ------------------------------------------------------------------------------------------------
// High Resolution Road Rendering
// Supports integer internal render scales from 2x through 4x.
// ------------------------------------------------------------------------------------------------
void HWRoad::render_background_hires(uint16_t* pixels)
{
    const int render_scale = std::clamp(config.video.hires + 1, 2, 4);
    const int width = config.s16_width;
    const uint16_t* roadram = ramBuff;

    for (int yy = 0; yy < S16_HEIGHT; ++yy)
    {
        const int data0 = roadram[0x000 + yy];
        const int data1 = roadram[0x100 + yy];
        int color = -1;

        switch (road_control & 3)
        {
            case 0:
                if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 1:
                if (data0 & 0x800) color = data0 & 0x7f;
                else if (data1 & 0x800) color = data1 & 0x7f;
                break;
            case 2:
                if (data1 & 0x800) color = data1 & 0x7f;
                else if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 3:
                if (data1 & 0x800) color = data1 & 0x7f;
                break;
        }

        if (color == -1)
            continue;

        const uint16_t c = static_cast<uint16_t>(color | color_offset3);
        const int first_y = yy * render_scale;
        for (int sub = 0; sub < render_scale; ++sub)
            std::fill_n(pixels + ((first_y + sub) * width), width, c);
    }
}

namespace
{
    inline int interpolate_wrapped(
        int current,
        int next,
        int fraction,
        int scale,
        int mask)
    {
        const int period = mask + 1;
        const int half = period >> 1;
        int delta = next - current;
        if (delta > half) delta -= period;
        else if (delta < -half) delta += period;

        // Round to the nearest integer step instead of truncating
        // towards zero. Plain integer division here systematically
        // biases every intermediate sub-scanline low (e.g. delta=2,
        // scale=3 gives steps of 0,0,1 instead of the much closer
        // 1,1,1 / 0,1,1 approximation of the true continuous line),
        // which is what produces the visible zigzag "Versatz" on the
        // road stripes/centre line once internal upscaling spreads
        // that error across several output rows.
        const int scaled = delta * fraction;
        const int rounded = (scaled >= 0)
            ? (scaled + scale / 2) / scale
            : -((-scaled + scale / 2) / scale);

        return (current + rounded) & mask;
    }
}

// ------------------------------------------------------------------------------------------------
// Render Road Foreground - High Resolution Version
// Intermediate output scanlines interpolate the source road line and horizontal
// position between adjacent native System 16 scanlines.
// ------------------------------------------------------------------------------------------------
void HWRoad::render_foreground_hires(uint16_t* pixels)
{
    const int render_scale = std::clamp(config.video.hires + 1, 2, 4);
    const int width = config.s16_width;
    const int logical_width = width / render_scale;
    uint16_t* roadram = ramBuff;

    static const uint8_t priority_map[2][8] =
    {
        { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
        { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
    };
    static const ALIGN64 uint8_t priority_lookup[8][8] =
    {
        { 0,0,0,0,0,0,0,1 },
        { 1,0,0,0,0,0,0,1 },
        { 1,0,0,0,0,0,0,1 },
        { 1,1,1,0,0,0,0,1 },
        { 0,0,0,0,0,0,0,0 },
        { 0,0,0,0,0,0,0,0 },
        { 0,0,0,0,0,0,0,0 },
        { 0,0,0,0,0,0,0,0 }
    };

    for (int y = 0; y < config.s16_height; ++y)
    {
        const int yy = y / render_scale;
        const int sub = y % render_scale;

        const uint32_t data0 = roadram[0x000 + yy];
        const uint32_t data1 = roadram[0x100 + yy];

        if ((data0 & 0x800) && (data1 & 0x800))
            continue;

        int hpos0 = roadram[0x200 +
            (((road_control & 4) != 0) ? yy : (data0 & 0x1ff))] & 0xfff;
        int hpos1 = roadram[0x400 +
            (((road_control & 4) != 0) ? (0x100 + yy) : (data1 & 0x1ff))] & 0xfff;

        uint8_t* src0 = (data0 & 0x800)
            ? roads + 256 * 2 * 512
            : roads + (0x000 + ((data0 >> 1) & 0xff)) * 512;
        uint8_t* src1 = (data1 & 0x800)
            ? roads + 256 * 2 * 512
            : roads + (0x100 + ((data1 >> 1) & 0xff)) * 512;

        if (sub != 0 && yy < S16_HEIGHT - 1)
        {
            const uint32_t next0 = roadram[0x000 + yy + 1];
            const uint32_t next1 = roadram[0x100 + yy + 1];

            if (!(data0 & 0x800) && !(next0 & 0x800))
            {
                const int line = interpolate_wrapped(
                    (data0 >> 1) & 0xff,
                    (next0 >> 1) & 0xff,
                    sub,
                    render_scale,
                    0xff);
                src0 = roads + (0x000 + line) * 512;

                const int next_hpos = roadram[0x200 +
                    (((road_control & 4) != 0) ? yy + 1 : (next0 & 0x1ff))] & 0xfff;
                hpos0 = interpolate_wrapped(
                    hpos0, next_hpos, sub, render_scale, 0xfff);
            }

            if (!(data1 & 0x800) && !(next1 & 0x800))
            {
                const int line = interpolate_wrapped(
                    (data1 >> 1) & 0xff,
                    (next1 >> 1) & 0xff,
                    sub,
                    render_scale,
                    0xff);
                src1 = roads + (0x100 + line) * 512;

                const int next_hpos = roadram[0x400 +
                    (((road_control & 4) != 0)
                        ? (0x100 + yy + 1)
                        : (next1 & 0x1ff))] & 0xfff;
                hpos1 = interpolate_wrapped(
                    hpos1, next_hpos, sub, render_scale, 0xfff);
            }
        }

        uint16_t color_table[32]{};
        const int color0 = roadram[0x600 +
            (((road_control & 4) != 0) ? yy : (data0 & 0x1ff))];
        const int color1 = roadram[0x600 +
            (((road_control & 4) != 0) ? (0x100 + yy) : (data1 & 0x1ff))];
        int bgcolor = (color0 >> 8) & 0xf;

        color_table[0x00] = color_offset1 ^ 0x00 ^ ((color0 >> 0) & 1);
        color_table[0x01] = color_offset1 ^ 0x02 ^ ((color0 >> 1) & 1);
        color_table[0x02] = color_offset1 ^ 0x04 ^ ((color0 >> 2) & 1);
        color_table[0x03] = (data0 & 0x200)
            ? color_table[0x00]
            : (color_offset2 ^ 0x00 ^ bgcolor);
        color_table[0x07] = color_offset1 ^ 0x06 ^ ((color0 >> 3) & 1);

        bgcolor = (color1 >> 8) & 0xf;
        color_table[0x10] = color_offset1 ^ 0x08 ^ ((color1 >> 4) & 1);
        color_table[0x11] = color_offset1 ^ 0x0a ^ ((color1 >> 5) & 1);
        color_table[0x12] = color_offset1 ^ 0x0c ^ ((color1 >> 6) & 1);
        color_table[0x13] = (data1 & 0x200)
            ? color_table[0x10]
            : (color_offset2 ^ 0x10 ^ bgcolor);
        color_table[0x17] = color_offset1 ^ 0x0e ^ ((color1 >> 7) & 1);

        const int control = road_control & 3;
        if ((control == 0 && (data0 & 0x800)) ||
            (control == 3 && (data1 & 0x800)))
        {
            continue;
        }

        const int s16_x = 0x5f8 + config.s16_x_off;
        int h0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
        int h1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
        uint16_t* out = pixels + (y * width);

        for (int x = 0; x < logical_width; ++x)
        {
            const unsigned pix0 = h0 < 0x200 ? src0[h0] : 3u;
            const unsigned pix1 = h1 < 0x200 ? src1[h1] : 3u;
            uint16_t colour = 0;

            switch (control)
            {
                case 0:
                    colour = color_table[pix0];
                    break;
                case 1:
                    colour = priority_lookup[pix0][pix1]
                        ? color_table[0x10 + pix1]
                        : color_table[pix0];
                    break;
                case 2:
                    colour = ((priority_map[1][pix0] >> pix1) & 1)
                        ? color_table[0x10 + pix1]
                        : color_table[pix0];
                    break;
                case 3:
                    colour = color_table[0x10 + pix1];
                    break;
            }

            std::fill_n(out + (x * render_scale), render_scale, colour);
            h0 = (h0 + 1) & 0xfff;
            h1 = (h1 + 1) & 0xfff;
        }
    }
}
