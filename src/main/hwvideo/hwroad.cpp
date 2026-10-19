#include <algorithm> // Required for std::fill_n
#include <cmath>
#include <cstdlib>
#include <iostream>
#include "hwvideo/hwroad.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"
#include "stereo.hpp"

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
        build_row_edges();
        build_row_widths();
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

namespace
{
    // The hi-res road smoothing never moves anything further than half a native
    // pixel away from the original data, so road and sprites cannot drift apart.
    constexpr int ROAD_SMOOTH_RADIUS = 2;
    constexpr int ROAD_SMOOTH_WEIGHTS[ROAD_SMOOTH_RADIUS * 2 + 1] = { 1, 2, 3, 2, 1 };

    inline int round_div(int value, int divisor)
    {
        return value >= 0 ? (value + divisor / 2) / divisor
                          : -((-value + divisor / 2) / divisor);
    }

    inline bool same_edge_sequence(const HWRoad::RowEdges& a, const HWRoad::RowEdges& b)
    {
        if (a.count < 0 || a.count != b.count || a.first_code != b.first_code)
            return false;
        for (int i = 0; i < a.count; i++)
            if (a.code[i] != b.code[i])
                return false;
        return true;
    }
}

// DX: record where the colour code changes within each decoded ROM row
// (including the dummy row). Rows with more transitions than the table holds
// are flagged and rendered unsmoothed by the hi-res road renderer.
void HWRoad::build_row_edges()
{
    for (int row = 0; row < 256 * 2 + 1; row++)
    {
        const uint8_t* src = roads + row * 512;
        RowEdges& edges = row_edges[row];
        edges.count = 0;
        edges.first_code = src[0];

        for (int x = 1; x < 512; x++)
        {
            if (src[x] == src[x - 1])
                continue;
            if (edges.count == ROW_MAX_EDGES)
            {
                edges.count = -1;
                break;
            }
            edges.pos[edges.count]  = static_cast<uint16_t>(x);
            edges.code[edges.count] = src[x];
            edges.count++;
        }
    }

    // The ROM stores edge positions rounded to whole pixels, which makes the
    // road width grow in irregular 1/2 pixel steps from row to row. Smooth each
    // edge across neighbouring rows of the same road with the same stripe
    // layout, limited to +-0.25 px of the original position. Together with the
    // +-0.5 px horizontal smoothing in smoothed_hpos_fp() no road edge ever
    // moves more than 0.75 native pixels from where the original renderer put it.
    for (int row = 0; row < 256 * 2 + 1; row++)
    {
        RowEdges& edges = row_edges[row];
        for (int i = 0; i < ROW_MAX_EDGES; i++)
            edges.pos16[i] = static_cast<int16_t>(edges.pos[i] * 16);
        if (edges.count <= 0 || row >= 512)
            continue;

        const int road_first = (row / 256) * 256;
        const int road_last  = road_first + 255;
        for (int i = 0; i < edges.count; i++)
        {
            int sum = 0, weight_sum = 0;
            for (int k = -ROAD_SMOOTH_RADIUS; k <= ROAD_SMOOTH_RADIUS; k++)
            {
                const int other = row + k;
                if (other < road_first || other > road_last)
                    continue;
                if (!same_edge_sequence(edges, row_edges[other]))
                    continue;
                const int w = ROAD_SMOOTH_WEIGHTS[k + ROAD_SMOOTH_RADIUS];
                sum += w * (row_edges[other].pos[i] - edges.pos[i]);
                weight_sum += w;
            }
            int delta16 = round_div(sum * 16, weight_sum);
            delta16 = std::clamp(delta16, -4, 4);
            edges.pos16[i] = static_cast<int16_t>(edges.pos[i] * 16 + delta16);
        }
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

        // Stereo 3D: per-scanline horizontal parallax (0 when not rendering an eye)
        const int sh0 = stereo_shift_logical(0, y);
        const int sh1 = stereo_shift_logical(1, y);

        // Shift road dependent on whether we are in widescreen mode or not
        uint16_t s16_x = 0x5f8 + config.s16_x_off;

        // draw the road
        switch (control)
        {
            case 0: {
                if (data0 & 0x800)
                    continue;
                uint16_t pairs = config.s16_width >> 1;
                uint32_t h0 = (hpos0 - (s16_x + x_offset) - sh0) & 0xfff;
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
                uint32_t h0 = (hpos0 - (s16_x + x_offset) - sh0) & 0xfff;
                uint32_t h1 = (hpos1 - (s16_x + x_offset) - sh1) & 0xfff;
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
                uint32_t h0 = (hpos0 - (s16_x + x_offset) - sh0) & 0xfff;
                uint32_t h1 = (hpos1 - (s16_x + x_offset) - sh1) & 0xfff;
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
                uint32_t h1 = (hpos1 - (s16_x + x_offset) - sh1) & 0xfff;
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
        return (current + (delta * fraction) / scale) & mask;
    }
}

// ------------------------------------------------------------------------------------------------
// Render Road Foreground - High Resolution Version
// Intermediate output scanlines interpolate the source road line and horizontal
// position between adjacent native System 16 scanlines.
// ------------------------------------------------------------------------------------------------
void HWRoad::render_foreground_hires(uint16_t* pixels)
{
    if (config.video.hiresroad)
    {
        render_foreground_hires_smooth(pixels);
        return;
    }

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

        // Stereo 3D: per-scanline horizontal parallax (0 when not rendering an eye)
        const int sh0 = stereo_shift_logical(0, yy);
        const int sh1 = stereo_shift_logical(1, yy);

        const int s16_x = 0x5f8 + config.s16_x_off;
        int h0 =(hpos0 - (s16_x + x_offset) - sh0) & 0xfff;
        int h1 = (hpos1 - (s16_x + x_offset) - sh1) & 0xfff;
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

// ------------------------------------------------------------------------------------------------
// Render Road Foreground - Smooth High Resolution Version (DX "ROAD ENHANCEMENT")
//
// Every native System 16 scanline is reproduced exactly as the original renderer
// draws it. Only the intermediate hi-res scanlines are refined:
//   - the horizontal road position is interpolated with 1/render_scale pixel
//     precision instead of being rounded to whole native pixels
//   - the road ROM row is not switched in whole steps; instead the colour
//     transitions (edges) of the two neighbouring ROM rows are interpolated
// Everything else (priorities, colours, solid fills, road RAM) is untouched, so
// sprites and shadows keep lining up with the road at native scanlines.
// ------------------------------------------------------------------------------------------------
namespace
{
    inline int wrapped_delta(int current, int next, int period)
    {
        const int half = period >> 1;
        int delta = next - current;
        if (delta > half) delta -= period;
        else if (delta < -half) delta += period;
        return delta;
    }

    // Expand one ROM row to render_scale pixels per native pixel. When 'b' is
    // given, edge positions are blended towards it by frac/render_scale.
    inline void expand_row_edges(
        const HWRoad::RowEdges& a,
        const HWRoad::RowEdges* b,
        int frac,
        int render_scale,
        uint8_t* vrow)
    {
        const int total = 512 * render_scale;
        int x = 0;
        unsigned code = a.first_code;
        for (int i = 0; i < a.count; i++)
        {
            int pos16 = a.pos16[i] * render_scale;
            if (b)
                pos16 += (b->pos16[i] - a.pos16[i]) * frac;
            int pos = (pos16 + 8) >> 4;
            if (pos > total) pos = total;
            if (pos > x)
            {
                std::fill_n(vrow + x, pos - x, static_cast<uint8_t>(code));
                x = pos;
            }
            code = a.code[i];
        }
        if (x < total)
            std::fill_n(vrow + x, total - x, static_cast<uint8_t>(code));
    }

    inline void expand_row_raw(const uint8_t* row, int render_scale, uint8_t* vrow)
    {
        for (int x = 0; x < 512; x++)
            std::fill_n(vrow + x * render_scale, render_scale, row[x]);
    }
}

// Horizontal road position of one native scanline in 1/render_scale pixels,
// smoothed across neighbouring valid scanlines. The ROM curve tables move the
// road back and forth by whole pixels from line to line; the filter removes
// that but never leaves +-0.5 native pixel of the original value.
int HWRoad::smoothed_hpos_fp(int road, int yy, int render_scale) const
{
    const bool direct = (road_control & 4) != 0;
    const int ram_base  = road * 0x100;
    const int hpos_base = 0x200 + road * 0x200;

    auto hpos_at = [&](int line, int& out) -> bool
    {
        if (line < 0 || line >= S16_HEIGHT)
            return false;
        const uint32_t d = ramBuff[ram_base + line];
        if (d & 0x800)
            return false;
        out = ramBuff[hpos_base + (direct ? (ram_base + line) : (d & 0x1ff))] & 0xfff;
        return true;
    };

    int centre = 0;
    hpos_at(yy, centre);

    int sum = 0, weight_sum = 0;
    for (int k = -ROAD_SMOOTH_RADIUS; k <= ROAD_SMOOTH_RADIUS; k++)
    {
        int v;
        if (!hpos_at(yy + k, v))
            continue;
        const int w = ROAD_SMOOTH_WEIGHTS[k + ROAD_SMOOTH_RADIUS];
        sum += w * wrapped_delta(centre, v, 0x1000);
        weight_sum += w;
    }

    int delta_fp = round_div(sum * render_scale, weight_sum);
    delta_fp = std::clamp(delta_fp, -(render_scale / 2), render_scale / 2);
    return centre * render_scale + delta_fp;
}

// Build the hi-res source line for one road at output scanline (yy, sub):
// 'vrow' receives 512 * render_scale colour codes, 'hpos_fp' the horizontal
// scroll in 1/render_scale native pixels. At sub == 0 both match the original
// renderer exactly.
void HWRoad::build_hires_road_line(
    int road, int yy, int sub, int render_scale, uint8_t* vrow, int& hpos_fp)
{
    const uint16_t* roadram = ramBuff;
    const int ram_base = road * 0x100;     // 0x000 / 0x100: line select
    const int row_base = road * 256;

    const uint32_t data = roadram[ram_base + yy];
    if (data & 0x800)
    {
        // Solid fill: behaves like the dummy row (all exterior)
        std::fill_n(vrow, 512 * render_scale, static_cast<uint8_t>(3));
        hpos_fp = 0;
        return;
    }

    const int line = (data >> 1) & 0xff;

    hpos_fp     = smoothed_hpos_fp(road, yy, render_scale);
    int line_fp = line * render_scale;

    if (sub != 0 && yy < S16_HEIGHT - 1)
    {
        const uint32_t next = roadram[ram_base + yy + 1];
        if (!(next & 0x800))
        {
            const int next_hpos_fp = smoothed_hpos_fp(road, yy + 1, render_scale);
            const int next_line = (next >> 1) & 0xff;
            hpos_fp += round_div(
                wrapped_delta(hpos_fp, next_hpos_fp, 0x1000 * render_scale) * sub,
                render_scale);
            line_fp += wrapped_delta(line, next_line, 0x100) * sub;
        }
    }

    // Split the fractional ROM row into base row + fraction (0..render_scale-1)
    int row_a = line_fp / render_scale;
    int frac  = line_fp - row_a * render_scale;
    if (frac < 0) { frac += render_scale; row_a--; }
    row_a &= 0xff;

    const RowEdges& a = row_edges[row_base + row_a];
    if (frac == 0 || row_a == 255)
    {
        if (a.count < 0) expand_row_raw(roads + (row_base + row_a) * 512, render_scale, vrow);
        else             expand_row_edges(a, nullptr, 0, render_scale, vrow);
        return;
    }

    const RowEdges& b = row_edges[row_base + row_a + 1];
    if (same_edge_sequence(a, b))
    {
        expand_row_edges(a, &b, frac, render_scale, vrow);
        return;
    }

    // Differing stripe layout between the two rows: use the nearer row as is
    const int nearest = (frac * 2 >= render_scale) ? row_a + 1 : row_a;
    const RowEdges& n = row_edges[row_base + nearest];
    if (n.count < 0) expand_row_raw(roads + (row_base + nearest) * 512, render_scale, vrow);
    else             expand_row_edges(n, nullptr, 0, render_scale, vrow);
}

void HWRoad::render_foreground_hires_smooth(uint16_t* pixels)
{
    const int render_scale = std::clamp(config.video.hires + 1, 2, 4);
    const int width = config.s16_width;
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

    const int scaled_period  = 0x1000 * render_scale;
    const int scaled_visible = 0x200 * render_scale;
    const int screen_offset_fp = (0x5f8 + config.s16_x_off + x_offset) * render_scale;

    static uint8_t vrow0[512 * 4];
    static uint8_t vrow1[512 * 4];

    for (int y = 0; y < config.s16_height; ++y)
    {
        const int yy = y / render_scale;
        const int sub = y % render_scale;

        const uint32_t data0 = roadram[0x000 + yy];
        const uint32_t data1 = roadram[0x100 + yy];

        if ((data0 & 0x800) && (data1 & 0x800))
            continue;

        const int control = road_control & 3;
        if ((control == 0 && (data0 & 0x800)) ||
            (control == 3 && (data1 & 0x800)))
        {
            continue;
        }

        const bool use0 = control != 3 && !(data0 & 0x800);
        const bool use1 = control != 0 && !(data1 & 0x800);

        int hpos0_fp = 0, hpos1_fp = 0;
        if (control != 3) build_hires_road_line(0, yy, sub, render_scale, vrow0, hpos0_fp);
        if (control != 0) build_hires_road_line(1, yy, sub, render_scale, vrow1, hpos1_fp);

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

        // Stereo 3D: per-scanline parallax with sub-native precision,
        // interpolated between native scanlines like the road itself.
        const int sh0 = stereo_shift_fp(0, yy, sub, render_scale);
        const int sh1 = stereo_shift_fp(1, yy, sub, render_scale);

        int h0 = (hpos0_fp - screen_offset_fp - sh0) % scaled_period;
        if (h0 < 0) h0 += scaled_period;
        int h1 = (hpos1_fp - screen_offset_fp - sh1) % scaled_period;
        if (h1 < 0) h1 += scaled_period;

        uint16_t* out = pixels + (y * width);

        for (int x = 0; x < width; ++x)
        {
            const unsigned pix0 = (use0 && h0 < scaled_visible) ? vrow0[h0] : 3u;
            const unsigned pix1 = (use1 && h1 < scaled_visible) ? vrow1[h1] : 3u;
            uint16_t colour;

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
                default:
                    colour = color_table[0x10 + pix1];
                    break;
            }

            out[x] = colour;
            if (++h0 == scaled_period) h0 = 0;
            if (++h1 == scaled_period) h1 = 0;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Stereo 3D support
//
// Each road ROM row is a pre-rendered perspective slice of the road. The width
// of the road surface within a row is proportional to inverse depth, so it can
// be used directly as the depth of every scanline that selects that row, on
// flat road, hills and splits alike.
//
// Set the environment variable CANNONBALL_STEREO_DEBUG=1 to log the measured
// road widths at start-up and the depth calibration every 120 frames.
// ------------------------------------------------------------------------------------------------

namespace
{
    bool stereo_debug_enabled()
    {
        static const bool enabled = std::getenv("CANNONBALL_STEREO_DEBUG") != nullptr;
        return enabled;
    }
}

void HWRoad::build_row_widths()
{
    for (int road = 0; road < 2; ++road)
    {
        // 1) Measure the road surface extent of every row (codes 0/1/2 are
        //    road surface and stripes, 3 is exterior, 7 the central marker).
        float measured[256];
        for (int row = 0; row < 256; ++row)
        {
            const uint8_t* src = roads + (road * 256 + row) * 512;
            int first = -1, last = -1;
            for (int x = 0; x < 512; ++x)
            {
                if ((src[x] & 3) != 3)
                {
                    if (first < 0) first = x;
                    last = x;
                }
            }
            measured[row] = (first < 0) ? 0.0f : static_cast<float>(last - first + 1);
        }

        // 2) Least-squares line through the unsaturated rows. Used for rows
        //    whose road runs off the 512 pixel ROM row or that are empty, so
        //    depth keeps increasing smoothly instead of clipping.
        double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int row = 0; row < 256; ++row)
        {
            const float w = measured[row];
            if (w > 4.0f && w < 500.0f)
            {
                n += 1; sx += row; sy += w; sxx += double(row) * row; sxy += double(row) * w;
            }
        }
        const double denom = n * sxx - sx * sx;
        const bool have_fit = n >= 16 && std::fabs(denom) > 1e-6;
        const double slope = have_fit ? (n * sxy - sx * sy) / denom : 0.0;
        const double icept = have_fit ? (sy - slope * sx) / n : 0.0;

        for (int row = 0; row < 256; ++row)
        {
            float w = measured[row];
            if (have_fit && (w <= 4.0f || w >= 500.0f))
                w = static_cast<float>(slope * row + icept);
            road_row_width[road * 256 + row] = std::max(w, 0.5f);
        }

        if (stereo_debug_enabled())
        {
            std::cout << "Stereo: road " << road << " row widths: fit "
                      << (have_fit ? "ok" : "unavailable")
                      << " (slope " << slope << ", intercept " << icept << ")"
                      << ", row0=" << measured[0] << " row64=" << measured[64]
                      << " row128=" << measured[128] << " row192=" << measured[192]
                      << " row255=" << measured[255] << std::endl;
        }
    }

    stereo_ref_width = 0.0f;
}

void HWRoad::stereo_prepare_frame(bool road_scene)
{
    const int control = road_control & 3;
    float line_w[2][STEREO_LINES];
    bool active = false;

    for (int road = 0; road < 2; ++road)
    {
        const bool road_enabled = road_scene && ((road == 0) ? (control != 3) : (control != 0));
        for (int yy = 0; yy < STEREO_LINES; ++yy)
        {
            const uint16_t data = ramBuff[road * 0x100 + yy];
            const bool valid = road_enabled && !(data & 0x800);
            stereo_valid[road][yy] = valid;
            line_w[road][yy] = valid ? road_row_width[road * 256 + ((data >> 1) & 0xff)] : 0.0f;
            active |= valid;
        }
    }
    stereo::state.road_active = active;

    if (!active)
    {
        stereo::samples.count = 0;
        for (int road = 0; road < 2; ++road)
            std::fill_n(stereo_inv[road], STEREO_LINES, stereo::INV_SCREEN);
        return;
    }

    auto width_at = [&](int yy) -> float
    {
        if (yy < 0 || yy >= STEREO_LINES) return 0.0f;
        if (stereo_valid[0][yy]) return line_w[0][yy];
        if (stereo_valid[1][yy]) return line_w[1][yy];
        return 0.0f;
    };

    // Fallback reference: the road just in front of the camera (bottom of the
    // screen), which is roughly where the Ferrari's wheels meet the road.
    float fallback = 0.0f;
    for (int yy = STEREO_LINES - 1; yy >= STEREO_LINES / 2 && fallback <= 0.0f; --yy)
        fallback = width_at(yy);

    // Auto-calibration: a sprite standing on the road at scanline y with zoom z
    // must have the same depth as the road at y, i.e.
    //     width(y) / ref == inv(sprite)   =>   ref = width(y) / inv(sprite)
    // Take the median estimate over this frame's samples and smooth over time.
    float estimates[stereo::Samples::MAX];
    int count = 0;
    for (int i = 0; i < stereo::samples.count; ++i)
    {
        const float w = width_at(stereo::samples.y[i]);
        const float inv = stereo::samples.inv[i];
        if (w > 2.0f && inv > 0.05f)
            estimates[count++] = w / inv;
    }
    stereo::samples.count = 0;

    if (count >= 3)
    {
        std::nth_element(estimates, estimates + count / 2, estimates + count);
        float est = estimates[count / 2];
        if (fallback > 0.0f)
            est = std::clamp(est, fallback * 0.5f, fallback * 2.0f);
        stereo_ref_width = (stereo_ref_width <= 0.0f)
            ? est
            : stereo_ref_width + 0.1f * (est - stereo_ref_width);
    }
    else if (stereo_ref_width <= 0.0f)
    {
        stereo_ref_width = fallback;
    }

    const float ref = stereo_ref_width > 0.0f ? stereo_ref_width : 1.0f;
    for (int road = 0; road < 2; ++road)
    {
        for (int yy = 0; yy < STEREO_LINES; ++yy)
        {
            // Lines without ROM road for this road borrow the other road's
            // depth (identical scanline); otherwise treat as distant.
            float w = line_w[road][yy];
            if (!stereo_valid[road][yy])
                w = width_at(yy);
            stereo_inv[road][yy] = w > 0.0f ? (w / ref) : stereo::INV_TILES_BACK;
        }
    }

    if (stereo_debug_enabled() && (++stereo_debug_counter % 120) == 0)
    {
        std::cout << "Stereo: ref width " << stereo_ref_width
                  << " (fallback " << fallback << ", " << count << " samples)"
                  << " inv@112=" << stereo_inv[0][112]
                  << " inv@160=" << stereo_inv[0][160]
                  << " inv@223=" << stereo_inv[0][223] << std::endl;
    }
}

int HWRoad::stereo_shift_logical(int road, int yy) const
{
    if (stereo::state.eye == stereo::EYE_NONE || yy < 0 || yy >= STEREO_LINES)
        return 0;
    return static_cast<int>(std::lround(stereo::shift_logical(stereo_inv[road][yy])));
}

int HWRoad::stereo_shift_fp(int road, int yy, int sub, int render_scale) const
{
    if (stereo::state.eye == stereo::EYE_NONE || yy < 0 || yy >= STEREO_LINES)
        return 0;

    float inv = stereo_inv[road][yy];
    if (sub != 0 && yy < STEREO_LINES - 1 && stereo_valid[road][yy] && stereo_valid[road][yy + 1])
        inv += (stereo_inv[road][yy + 1] - inv) * (static_cast<float>(sub) / render_scale);

    return stereo::shift_render(inv, render_scale);
}
