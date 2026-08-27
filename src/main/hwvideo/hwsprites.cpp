#include "video.hpp"
#include "hwvideo/hwsprites.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"
#include <chrono>

/***************************************************************************
    Video Emulation: OutRun Sprite Rendering Hardware.
    Based on MAME source code.

    Copyright Aaron Giles.
    All rights reserved.
***************************************************************************/

/* This version is tuned for CannonBall-SE, modifications Copyright (c) 2025, James Pearce
   Approx. 40% speed-up on Pi2 */

/*******************************************************************************************
*  Out Run/X-Board-style sprites
*
*      Offs  Bits               Usage
*       +0   e------- --------  Signify end of sprite list
*       +0   -h-h---- --------  Hide this sprite if either bit is set
*       +0   --c----- --------  JJP - clip required (partially off-screen)
*       +0   ----bbb- --------  Sprite bank
*       +0   -------t tttttttt  Top scanline of sprite + 256
*       +2   oooooooo oooooooo  Offset within selected sprite bank
*       +4   ppppppp- --------  Signed 7-bit pitch value between scanlines
*       +4   -------x xxxxxxxx  X position of sprite (position $BE is screen position 0)
*       +6   -s------ --------  Enable shadows
*       +6   --pp---- --------  Sprite priority, relative to tilemaps
*       +6   ----vvvv vvvvvvvv  Zoom factor (0x200 = full size, 0x100 = half size, 0x300 = 2x size) - JJP - this is wrong, 0x100 is 2x size
*       +8   y------- --------  Render from top-to-bottom (1) or bottom-to-top (0) on screen
*       +8   -f------ --------  Horizontal flip: read the data backwards if set
*       +8   --x----- --------  Render from left-to-right (1) or right-to-left (0) on screen
//*       +8   ------hh hhhhhhhh  Horizontal zoom factor (0x200 = full size, 0x100 = half size, 0x300 = 2x size)
*       +8   ------ww wwwwwwww  Sprite width on-screen (in native res)
*       +E   dddddddd dddddddd  Scratch space for current address
*
*  Out Run only:
*       +A   hhhhhhhh --------  Height in scanlines - 1
*       +A   -------- -ccccccc  Sprite color palette
*
*  X-Board only:
*       +A   ----hhhh hhhhhhhh  Height in scanlines - 1
*       +C   -------- cccccccc  Sprite color palette
*
*  Final bitmap format:
*
*            -s------ --------  Shadow control
*            --pp---- --------  Sprite priority
*            ----cccc cccc----  Sprite color palette
*            -------- ----llll  4-bit pixel data
*
 *******************************************************************************************/

// Enable for hardware pixel accuracy, where sprite shadowing delayed by 1 clock cycle (slower)
#define PIXEL_ACCURACY 0

#include <stdio.h>
#include <stdint.h>


#define DEFINE_RW_FUNCS(TYPE, SUFFIX)                                      \
void write_bytes_##SUFFIX(const char *filename, const TYPE *data, uint32_t length) { \
    FILE *f = fopen(filename, "wb");                                      \
    if (!f) { perror("fopen"); return; }                                  \
    fwrite(data, sizeof(TYPE), length, f);                                \
    fclose(f);                                                            \
}                                                                         \
                                                                          \
void read_bytes_##SUFFIX(const char *filename, TYPE *data, uint32_t length) { \
    FILE *f = fopen(filename, "rb");                                      \
    if (!f) { perror("fopen"); return; }                                  \
    fread(data, sizeof(TYPE), length, f);                                 \
    fclose(f);                                                            \
}

DEFINE_RW_FUNCS(uint8_t,  u8)
DEFINE_RW_FUNCS(uint16_t, u16)
DEFINE_RW_FUNCS(uint32_t, u32)



hwsprites::hwsprites()
{
}

hwsprites::~hwsprites()
{
}


void hwsprites::init(const uint8_t* src_sprites)
{
    // A nullptr source is a renderer-only restart. Keep the live sprite RAM
    // and already decoded graphics intact so display-mode changes are seamless.
    if (src_sprites)
    {
        reset();
        // Convert S16 tiles to a more useable format
        const uint8_t *spr = src_sprites;

        for (uint32_t i = 0; i < SPRITES_LENGTH; i++)
        {
            uint8_t d3 = *spr++;
            uint8_t d2 = *spr++;
            uint8_t d1 = *spr++;
            uint8_t d0 = *spr++;

            // Forward (just endian swap of bytes, keep pixel order p0..p7)
            sprites[i] = ((uint32_t)d0 << 24) |
                         ((uint32_t)d1 << 16) |
                         ((uint32_t)d2 <<  8) |
                         ((uint32_t)d3 <<  0);
        }
    }
//    read_bytes_u32("sprites_flipped.bin", sprites_flipped, SPRITES_LENGTH);
//    read_bytes_u8("sprites_shadows.bin", sprites_shadows, SPRITES_LENGTH);
}


void hwsprites::reset()
{
    // Clear Sprite RAM buffers
    std::fill_n(ram,                 std::size(ram),                0x0);
    std::fill_n(ramBuff,             std::size(ramBuff),            0x0);
    std::fill_n(sprites_flipped,     std::size(sprites_flipped),    0xffffffff);
    std::fill_n(sprites_shadowinfo,  std::size(sprites_shadowinfo), 0xff);
}

// Clip areas of the screen in wide-screen mode
void hwsprites::set_x_clip(bool on)
{
    // Clip to central 320 width window.
    if (on)
    {
        x1 = config.s16_x_off;
        x2 = x1 + S16_WIDTH;

        if (config.video.hires)
        {
            x1 <<= 1;
            x2 <<= 1;
        }
    }
    // Allow full wide-screen.
    else
    {
        x1 = 0;
        x2 = config.s16_width;
    }
}

uint8_t hwsprites::read(const uint16_t adr)
{
    uint16_t a = adr >> 1;
    if ((adr & 1) == 1)
        return ram[a] & 0xff;
    else
        return ram[a] >> 8;
}

void hwsprites::write(const uint16_t adr, const uint16_t data)
{
    ram[adr >> 1] = data;
}

// Copy back buffer to main ram, ready for blit
void hwsprites::swap()
{
    uint16_t *src = (uint16_t *)ram;
    uint16_t *dst = (uint16_t *)ramBuff;

    // swap the halves of the road RAM
    for (uint16_t i = 0; i < SPRITE_RAM_SIZE; i++)
    {
        uint16_t temp = *src;
        *src++ = *dst;
        *dst++ = temp;
    }
}

#if PIXEL_ACCURACY

// Reproduces glowy edge around sprites on top of shadows as seen on Hardware.
// Believed to be caused by shadowing being out by one clock cycle / pixel.
//
// 1/ Sprites Drawn on top of Shadow clears the shadow flags for its opaque pixels.
// 2/ Either the flag clear or the sprite itself is offset by one pixel horizontally.
//
// Thanks to Alex B. for this implementation.

#define draw_pixel()                                                                                  \
{                                                                                                     \
    if (x >= x1 && x < x2)                                                                            \
    {                                                                                                 \
        if (shadow && pix == 0xa)                                                                     \
        {                                                                                             \
            pPixel[x] &= 0xfff;                                                                       \
            pPixel[x] += S16_PALETTE_ENTRIES;                                                         \
        }                                                                                             \
        else if (pix != 0 && pix != 15)                                                               \
        {                                                                                             \
            if (x > x1) pPixel[x-1] &= 0xfff;                                                         \
            pPixel[x] = (pix | color);                                                                \
        }                                                                                             \
    }                                                                                                 \
}

#else

// MACROS WITH SHADOW SUPPORT

#define IF_IS_DRAWABLE_()        if (x >= x1 && x < x2 && pix != 0 && pix != 15)
#define IF_IS_DRAWABLE()         if (((unsigned)(pix - 1u) < 14u) && ((unsigned)(x - x1) < span))
#define IS_DRAWABLE()               (((unsigned)(pix - 1u) < 14u) && ((unsigned)(x - x1) < span))
#define IF_IS_DRAWABLE_NO_CLIP() if  ((unsigned)(pix - 1u) < 14u)
#define IS_DRAWABLE_NO_CLIP()        ((unsigned)(pix - 1u) < 14u)

#define draw_pixel_1row()                                       \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE() {                                      \
            if (pix == 0xa) {                                   \
                *pPix1 |= S16_PALETTE_ENTRIES;                  \
            } else {                                            \
                *pPix1 = (pix | color);                         \
            }                                                   \
        }                                                       \
        pPix1++;                                                \
        x++; xacc += zoom;                                      \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_2row()                                       \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE() {                                      \
            if (pix == 0xa) {                                   \
                *pPix1 |= S16_PALETTE_ENTRIES;                  \
                *pPix2 |= S16_PALETTE_ENTRIES;                  \
            } else {                                            \
                *pPix1 = (pix | color);                         \
                *pPix2 = (pix | color);                         \
            }                                                   \
        }                                                       \
        pPix1++; pPix2++;                                       \
        x++; xacc += zoom;                                      \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_3row()                                       \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE() {                                      \
            if (pix == 0xa) {                                   \
                *pPix1 |= S16_PALETTE_ENTRIES;                  \
                *pPix2 |= S16_PALETTE_ENTRIES;                  \
                *pPix3 |= S16_PALETTE_ENTRIES;                  \
            } else {                                            \
                *pPix1 = (pix | color);                         \
                *pPix2 = (pix | color);                         \
                *pPix3 = (pix | color);                         \
            }                                                   \
        }                                                       \
        pPix1++; pPix2++; pPix3++;                              \
        x++; xacc += zoom;                                      \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_1row_nc()                                    \
{                                                               \
int i = 0;\
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE_NO_CLIP() {                              \
i++;\
            if (pix == 0xa) {                                   \
                *pPix1 |= S16_PALETTE_ENTRIES;                  \
            } else {                                            \
                *pPix1 = (pix | color);                         \
            }                                                   \
        }                                                       \
        pPix1++;                                                \
        xacc += zoom;                                           \
    }                                                           \
    xacc -= 0x200;                                              \
reps[i]++;\
}

#define draw_pixel_2row_nc()                                    \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE_NO_CLIP() {                              \
            if (pix == 0xa) {                                   \
                *pPix1 |= S16_PALETTE_ENTRIES;                  \
                *pPix2 |= S16_PALETTE_ENTRIES;                  \
            } else {                                            \
                *pPix1 = (pix | color);                         \
                *pPix2 = (pix | color);                         \
            }                                                   \
        }                                                       \
        pPix1++; pPix2++;                                       \
        xacc += zoom;                                           \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_3row_nc()                                    \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE_NO_CLIP() {                              \
            if (pix == 0xa) {                                   \
                *pPix1 |= S16_PALETTE_ENTRIES;                  \
                *pPix2 |= S16_PALETTE_ENTRIES;                  \
                *pPix3 |= S16_PALETTE_ENTRIES;                  \
            } else {                                            \
                *pPix1 = (pix | color);                         \
                *pPix2 = (pix | color);                         \
                *pPix3 = (pix | color);                         \
            }                                                   \
        }                                                       \
        pPix1++; pPix2++; pPix3++;                              \
        xacc += zoom;                                           \
    }                                                           \
    xacc -= 0x200;                                              \
}

// MACROS WITHOUT SHADOW SUPPORT

#define draw_pixel_1row_ns()                                    \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE() {                                      \
            *pPix1 = (pix | color);                             \
        }                                                       \
        pPix1++;                                                \
        x++; xacc += zoom;                                      \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_2row_ns()                                    \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE() {                                      \
            *pPix1 = (pix | color);                             \
            *pPix2 = (pix | color);                             \
        }                                                       \
        pPix1++; pPix2++;                                       \
        x++; xacc += zoom;                                      \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_3row_ns()                                    \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE() {                                      \
            *pPix1 = (pix | color);                             \
            *pPix2 = (pix | color);                             \
            *pPix3 = (pix | color);                             \
        }                                                       \
        pPix1++; pPix2++; pPix3++;                              \
        x++; xacc += zoom;                                      \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_1row_nc_ns()                                 \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE_NO_CLIP() {                              \
            *pPix1 = (pix | color);                             \
        }                                                       \
        pPix1++;                                                \
        xacc += zoom;                                           \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_2row_nc_ns()                                 \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE_NO_CLIP() {                              \
            *pPix1 = (pix | color);                             \
            *pPix2 = (pix | color);                             \
        }                                                       \
        pPix1++; pPix2++;                                       \
        xacc += zoom;                                           \
    }                                                           \
    xacc -= 0x200;                                              \
}

#define draw_pixel_3row_nc_ns()                                 \
{                                                               \
    while (xacc < 0x200) {                                      \
        IF_IS_DRAWABLE_NO_CLIP() {                              \
            *pPix1 = (pix | color);                             \
            *pPix2 = (pix | color);                             \
            *pPix3 = (pix | color);                             \
        }                                                       \
        pPix1++; pPix2++; pPix3++;                              \
        xacc += zoom;                                           \
    }                                                           \
    xacc -= 0x200;                                              \
}

#endif



void hwsprites::render(uint16_t* pixels, const uint8_t priority)
{
    static uint32_t reps[6] = {0,0,0,0,0,0};

    static uint32_t freq[32];
    const uint32_t numbanks = SPRITES_LENGTH / 0x10000;

    for (uint16_t data = 0; data < SPRITE_RAM_SIZE; data += 16) // was +=8
    {
        auto start = std::chrono::high_resolution_clock::now();
        // stop when we hit the end of sprite list
        if ((ramBuff[data+0] & 0x8000) != 0) break;

        uint32_t sprpri  = 1 << ((ramBuff[data+3] >> 12) & 3);
        if (sprpri != priority) continue;

        // if hidden, or top greater than/equal to bottom, or invalid bank, punt
        int16_t hide    = (ramBuff[data+0] & 0x5000);
        int32_t height  = (ramBuff[data+5] >> 8) + 1;
        if (hide != 0 || height == 0) continue;

        int16_t bank      =  (ramBuff[data+0] >> 9) & 7;
        int32_t top       =  (ramBuff[data+0] & 0x1ff) - 0x100;
        bool     clip     =  (ramBuff[data+0] & 0x2000);
        uint32_t addr     =   ramBuff[data+1];
        int32_t pitch     = ((ramBuff[data+2] >> 1) | ((ramBuff[data+4] & 0x1000) << 3)) >> 8;
        uint8_t shadow    =  (ramBuff[data+3] >> 14) & 1;
//        int32_t  zoom     =   ramBuff[data+3] & 0x7ff;
        int32_t  zoom     =   ramBuff[data+3] & 0x0fff;
        int32_t ydelta    = ((ramBuff[data+4] & 0x8000) != 0) ? 1 : -1;
        int32_t flip      = (~ramBuff[data+4] >> 14) & 1;
        int32_t xdelta    = ((ramBuff[data+4] & 0x2000) != 0) ? 1 : -1;
//        int32_t hzoom     =   ramBuff[data+4] & 0x7ff;
        int32_t width     =   ramBuff[data+4] & 0x7ff;
        int32_t color     = COLOR_BASE +
                            ((ramBuff[data+5] & 0x7f) << 4);
        int32_t sprite_scanlines =
                            ((ramBuff[data+5] & 0xff00) >> 16) +1;
        int32_t xpos      =   ramBuff[data+6]; // moved from original structure to accomodate widescreen
        int32_t rawh      =   ramBuff[data+7]; // JJP - height of sprite in rows
        int32_t y, ytarget, yacc = 0;

        int16_t offset = ramBuff[data + 15];

        // adjust X coordinate
        // note: the threshhold below is a guess. If it is too high, rachero will draw garbage
        // If it is too low, smgp won't draw the bottom part of the road
        if (config.video.widescreen != 2 && xpos < 0x80 && xdelta < 0)
            xpos += 0x200;

        xpos -= 0xbe;

 //       // initialize the end address to the start address
 //       ramBuff[data+7] = addr;

        // clamp to within the memory region size
        if (numbanks)
            bank %= numbanks;

        const uint32_t* spriterom = sprites + 0x10000 * bank;
        uint32_t* spriterom_flipped = sprites_flipped + 0x10000 * bank;
        uint8_t* spriterom_shadowinfo = sprites_shadowinfo + 0x10000 * bank;

        // loop from top to bottom
        ytarget = top + ydelta * height;

        // Adjust for widescreen mode
        xpos += config.s16_x_off;

        // Adjust for hi-res mode
        if (config.video.hires) {
            xpos <<= 1;
            top <<= 1;
            ytarget <<= 1;
            zoom >>= 1;
        }

        // JJP - maintain converted sprites to aid writing them
        uint32_t sprite_height = 0;
        // Determine underlying sprite height
        int32_t steps = ytarget - top;  // true as ydelta is always + or - 1.
        // Use 64-bit multiply to avoid overflow
        sprite_height = ((uint64_t)steps * zoom) >> 9;

//std::cout << "\rSprite height: " << height << ", Calculated Height: " << sprite_height << ", raw height: " << rawh << "\n";

/* The above replaces...
        uint32_t yacc_t = 0;
        for (int y = top; y != ytarget; y += ydelta) {
            yacc_t += zoom;
            sprite_height += (yacc_t >> 9);
            yacc_t &= 0x1ff;
        }
*/
        static uint32_t converted_sprites = 0;
        static uint32_t processed_lines = 0;

        if (xdelta == -1) {
            xdelta = 1;       // always draw left-to-right
            xpos  -= (width << (config.video.hires ? 1 : 0));   // move draw position left by rendered width
            if (flip) {
                flip = 0;
                addr -= (pitch - 1);
            } else {
                flip = 1;
                addr += (pitch - 1);
            }
        }

        uint32_t shadowaddr = addr;

        if (flip) {
/*
            converted_sprites++;
            if (converted_sprites==1024) {
                write_bytes_u32("sprites_flipped.bin", sprites_flipped, SPRITES_LENGTH);
                write_bytes_u8("sprites_shadowinfo.bin", sprites_shadowinfo, SPRITES_LENGTH);
                converted_sprites=0;
            }
*/
            // first encounter of sprite, created flipped entry
            shadowaddr -= (pitch - 1); // start of data, if unflipped

            for (int y = 0; y < sprite_height; y++) {
//            for (int y = 0; y < height; y++) {
                // calculate the addresses for this line
                uint32_t readaddr     = shadowaddr;
                uint32_t writeaddr    = shadowaddr + pitch;
                uint32_t shadow_found = 0;
                if (spriterom_flipped[readaddr] == 0xffffffff) {
/*
processed_lines++;
std::cout << "\r\t\t\t\t" << processed_lines << " sprite lines flipped";
*/
                    // first time we've seen this row
                    for (int x = 0; x < pitch; x++) {
                        uint32_t pixels = spriterom[readaddr++];
                        uint8_t  px[8];
                        // check for shadows
                        uint32_t pxt = pixels ^ 0xAAAAAAAAu;
                        if (((pxt - 0x11111111u) & ~pxt & 0x88888888u) != 0)
                            shadow_found = 0x11;
                        // process eight pixels
                        px[0] = (pixels >> 28) & 0xf;
                        px[1] = (pixels >> 24) & 0xf;
                        px[2] = (pixels >> 20) & 0xf;
                        px[3] = (pixels >> 16) & 0xf;
                        px[4] = (pixels >> 12) & 0xf;
                        px[5] = (pixels >>  8) & 0xf;
                        px[6] = (pixels >>  4) & 0xf;
                        px[7] = (pixels >>  0) & 0xf;

                        spriterom_flipped[--writeaddr] =
                            (px[0] <<  0) |
                            (px[1] <<  4) |
                            (px[2] <<  8) |
                            (px[3] << 12) |
                            (px[4] << 16) |
                            (px[5] << 20) |
                            (px[6] << 24) |
                            (px[7] << 28);
                    };
                    spriterom_shadowinfo[shadowaddr] = shadow_found;
                }
                shadowaddr += pitch;
            }
        } else {
            // not flipped
            for (int y = 0; y < sprite_height; y++) {
                if (spriterom_shadowinfo[shadowaddr] == 0xff) {
                    // first time seeing this sprite line
                    uint32_t readaddr = shadowaddr;
                    uint32_t shadow_found = 0;
                    for (int i = 0; i < pitch; ++i) {
                        uint32_t pixels = spriterom[readaddr++];
                        // check for shadows
                        uint32_t pxt = pixels ^ 0xAAAAAAAAu;
                        if (((pxt - 0x11111111u) & ~pxt & 0x88888888u) != 0) {
                            shadow_found = 0x11;
                            break;
                        }
                    }
                    spriterom_shadowinfo[shadowaddr] = shadow_found;
                }
                shadowaddr += pitch;
            }
        }

        // adjust x-position with pre-determined offset for hi-res sprite rendering
        if (config.video.hiresprites == 1)
            xpos += offset;

        // choose which ROM to read from - flipped or non-flipped
        const uint32_t* spritedata;
        if (flip) {
            addr = addr - pitch + 1; // set pointer to start of sprite row
            spritedata = &spriterom_flipped[0];
        } else {
            spritedata = &spriterom[0];
        }

        const uint16_t scrn_width = config.s16_width;
        const unsigned span = (unsigned)(x2 - x1);

//        setup += std::chrono::high_resolution_clock::now() - start;
//        start = std::chrono::high_resolution_clock::now();
        uint32_t jump_key = 3;
        // drawing loop
        for (y = top; y != ytarget; y += ydelta)
        {
            // skip drawing if not within the cliprect
            if (y >= 0 && y < config.s16_height)
            {
                uint16_t* pPix1 = pixels + ((y+0) * scrn_width) + xpos;
                uint32_t spriteaddr = addr;
                int32_t xacc = 0;

                // determine how many rows will be written on this pass
                uint16_t count = 1;
                uint32_t frac = yacc + zoom;
                uint16_t countmax = std::min( (uint16_t)(ytarget - y),
                                              std::min<uint16_t>(3, config.s16_height - y));
                while (frac < 0x200 && count < countmax) {
                    frac += zoom;
                    count++;
                }
                // count now contains a value between 1 and 3.
                // we can also use (count-1) as the *minimum* number of pixels that will be written out
                // but each x-pass, since hzoom=vzoom.

                bool shadowfound = shadow && (spriterom_shadowinfo[addr] == 0x11);

                // the following should compile to a jump table, making it much quicker to get to the
                // optimised drawing path we need without wading through layers of if/else etc

                count = (count - 1) & 3; // 0..3
                //uint32_t jump_key = //((xdelta>0)     ? 0u : 1u)   /* draw direction */
                jump_key =   (count                   ) | /* output rows, 1-4, selected via 0-3 */
                             ((clip)         ? 0u : 4u) | /* sprite requires x-clip */
                             ((shadowfound)  ? 8u : 0u); /* sprite has shadows */

                // Note - the proportion of sprite *lines* following the (shadowfound) path (jump_key>7) is
                // low, approx 10%, however the cost of rendering them is much, much higher.

                freq[jump_key]++;

//std::cout << "Clip: " << clip << ", Count: " << count << ", flip: " << flip << ", jump_key: " << jump_key << std::endl;

                switch (jump_key) {

                    case 0:
                    {
                        // no shadows, clipped, count==1, not flipped
                        for (int32_t x = xpos; (xdelta > 0 && x < scrn_width) || (xdelta < 0 && x >= 0); ) {
                            uint32_t pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_1row_ns();
                            pix = (pixels >> 24) & 0xf; draw_pixel_1row_ns();
                            pix = (pixels >> 20) & 0xf; draw_pixel_1row_ns();
                            pix = (pixels >> 16) & 0xf; draw_pixel_1row_ns();
                            pix = (pixels >> 12) & 0xf; draw_pixel_1row_ns();
                            pix = (pixels >>  8) & 0xf; draw_pixel_1row_ns();
                            pix = (pixels >>  4) & 0xf; draw_pixel_1row_ns();
                            pix = (pixels >>  0) & 0xf; draw_pixel_1row_ns();
                            // stop if the second-to-last pixel in the group was 0xf
                            if ((pixels & 0x000000f0) == 0x000000f0)
                                break;
                        }
                        break;
                    }
                    case 1:
                    {
                        // no shadows, clipped, count==2, not-flipped
                        uint16_t* pPix2 = pixels + ((y+1) * scrn_width) + xpos;
                        for (int32_t x = xpos; (xdelta > 0 && x < scrn_width) || (xdelta < 0 && x >= 0); ) {
                            uint32_t pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_2row_ns();
                            pix = (pixels >> 24) & 0xf; draw_pixel_2row_ns();
                            pix = (pixels >> 20) & 0xf; draw_pixel_2row_ns();
                            pix = (pixels >> 16) & 0xf; draw_pixel_2row_ns();
                            pix = (pixels >> 12) & 0xf; draw_pixel_2row_ns();
                            pix = (pixels >>  8) & 0xf; draw_pixel_2row_ns();
                            pix = (pixels >>  4) & 0xf; draw_pixel_2row_ns();
                            pix = (pixels >>  0) & 0xf; draw_pixel_2row_ns();
                            // stop if the second-to-last pixel in the group was 0xf
                            if ((pixels & 0x000000f0) == 0x000000f0)
                                break;
                        }
                        // accumulate extra zoom factor
                        yacc += zoom;
                        addr += pitch * (yacc >> 9);
                        yacc &= 0x1ff;
                        y++;
                        break;
                    }
                    case 2:
                    {
                        // no shadows, clipped, count==3, not-flipped
                        uint16_t* pPix2 = pixels + ((y+1) * scrn_width) + xpos;
                        uint16_t* pPix3 = pixels + ((y+2) * scrn_width) + xpos;
                        for (int32_t x = xpos; (xdelta > 0 && x < scrn_width) || (xdelta < 0 && x >= 0); ) {
                            uint32_t pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_3row_ns();
                            pix = (pixels >> 24) & 0xf; draw_pixel_3row_ns();
                            pix = (pixels >> 20) & 0xf; draw_pixel_3row_ns();
                            pix = (pixels >> 16) & 0xf; draw_pixel_3row_ns();
                            pix = (pixels >> 12) & 0xf; draw_pixel_3row_ns();
                            pix = (pixels >>  8) & 0xf; draw_pixel_3row_ns();
                            pix = (pixels >>  4) & 0xf; draw_pixel_3row_ns();
                            pix = (pixels >>  0) & 0xf; draw_pixel_3row_ns();
                            // stop if the second-to-last pixel in the group was 0xf
                            if ((pixels & 0x000000f0) == 0x000000f0)
                                break;
                        }
                        // accumulate extra zoom factors
                        for (int i=1; i<2; i++) {
                            yacc += zoom;
                            addr += pitch * (yacc >> 9);
                            yacc &= 0x1ff;
                            y++;
                        }
                        break;
                    }
                    case 3: break; // count==4 - not used as <1%
                    case 4:
                    {
                        // no shadows, not clipped, count==1, not flipped
                        //for (int32_t x = xpos; (xdelta > 0 && x < scrn_width) || (xdelta < 0 && x >= 0); ) {
                        uint32_t pixels;
                        do {
                            pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_1row_nc_ns();
                            pix = (pixels >> 24) & 0xf; draw_pixel_1row_nc_ns();
                            pix = (pixels >> 20) & 0xf; draw_pixel_1row_nc_ns();
                            pix = (pixels >> 16) & 0xf; draw_pixel_1row_nc_ns();
                            pix = (pixels >> 12) & 0xf; draw_pixel_1row_nc_ns();
                            pix = (pixels >>  8) & 0xf; draw_pixel_1row_nc_ns();
                            pix = (pixels >>  4) & 0xf; draw_pixel_1row_nc_ns();
                            pix = (pixels >>  0) & 0xf; draw_pixel_1row_nc_ns();
                            // stop if the second-to-last pixel in the group was 0xf
                        } while ((pixels & 0x000000f0) != 0x000000f0);
                        break;
                    }
                    case 5:
                    {
                        // no shadows, not clipped, count==2, not flipped
                        uint16_t* pPix2 = pixels + ((y+1) * scrn_width) + xpos;
                        uint32_t pixels;
                        do {
                            pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_2row_nc_ns();
                            pix = (pixels >> 24) & 0xf; draw_pixel_2row_nc_ns();
                            pix = (pixels >> 20) & 0xf; draw_pixel_2row_nc_ns();
                            pix = (pixels >> 16) & 0xf; draw_pixel_2row_nc_ns();
                            pix = (pixels >> 12) & 0xf; draw_pixel_2row_nc_ns();
                            pix = (pixels >>  8) & 0xf; draw_pixel_2row_nc_ns();
                            pix = (pixels >>  4) & 0xf; draw_pixel_2row_nc_ns();
                            pix = (pixels >>  0) & 0xf; draw_pixel_2row_nc_ns();
                            // stop if the second-to-last pixel in the group was 0xf
                        } while ((pixels & 0x000000f0) != 0x000000f0);
                        // accumulate extra zoom factor
                        yacc += zoom;
                        addr += pitch * (yacc >> 9);
                        yacc &= 0x1ff;
                        y++;
                        break;
                    }
                    case 6:
                    {
                        // no shadows, not clipped, count==3, not flipped
                        uint16_t* pPix2 = pixels + ((y+1) * scrn_width) + xpos;
                        uint16_t* pPix3 = pixels + ((y+2) * scrn_width) + xpos;
                        uint32_t pixels;
                        do {
                            pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_3row_nc_ns();
                            pix = (pixels >> 24) & 0xf; draw_pixel_3row_nc_ns();
                            pix = (pixels >> 20) & 0xf; draw_pixel_3row_nc_ns();
                            pix = (pixels >> 16) & 0xf; draw_pixel_3row_nc_ns();
                            pix = (pixels >> 12) & 0xf; draw_pixel_3row_nc_ns();
                            pix = (pixels >>  8) & 0xf; draw_pixel_3row_nc_ns();
                            pix = (pixels >>  4) & 0xf; draw_pixel_3row_nc_ns();
                            pix = (pixels >>  0) & 0xf; draw_pixel_3row_nc_ns();
                            // stop if the second-to-last pixel in the group was 0xf
                        } while ((pixels & 0x000000f0) != 0x000000f0);
                        // accumulate extra zoom factors
                        for (int i=1; i<2; i++) {
                            yacc += zoom;
                            addr += pitch * (yacc >> 9);
                            yacc &= 0x1ff;
                            y++;
                        }
                        break;
                    }
                    case 7: break; // count==4 - not used as <1%
                    case 8:
                    {
                        // shadows, clipped, count==1, not flipped
                        for (int32_t x = xpos; (xdelta > 0 && x < scrn_width) || (xdelta < 0 && x >= 0); ) {
                            uint32_t pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_1row();
                            pix = (pixels >> 24) & 0xf; draw_pixel_1row();
                            pix = (pixels >> 20) & 0xf; draw_pixel_1row();
                            pix = (pixels >> 16) & 0xf; draw_pixel_1row();
                            pix = (pixels >> 12) & 0xf; draw_pixel_1row();
                            pix = (pixels >>  8) & 0xf; draw_pixel_1row();
                            pix = (pixels >>  4) & 0xf; draw_pixel_1row();
                            pix = (pixels >>  0) & 0xf; draw_pixel_1row();
                            // stop if the second-to-last pixel in the group was 0xf
                            if ((pixels & 0x000000f0) == 0x000000f0)
                                break;
                        }
                        break;
                    }
                    case 9:
                    {
                        // shadows, clipped, count==2, not-flipped
                        uint16_t* pPix2 = pixels + ((y+1) * scrn_width) + xpos;
                        for (int32_t x = xpos; (xdelta > 0 && x < scrn_width) || (xdelta < 0 && x >= 0); ) {
                            uint32_t pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_2row();
                            pix = (pixels >> 24) & 0xf; draw_pixel_2row();
                            pix = (pixels >> 20) & 0xf; draw_pixel_2row();
                            pix = (pixels >> 16) & 0xf; draw_pixel_2row();
                            pix = (pixels >> 12) & 0xf; draw_pixel_2row();
                            pix = (pixels >>  8) & 0xf; draw_pixel_2row();
                            pix = (pixels >>  4) & 0xf; draw_pixel_2row();
                            pix = (pixels >>  0) & 0xf; draw_pixel_2row();
                            // stop if the second-to-last pixel in the group was 0xf
                            if ((pixels & 0x000000f0) == 0x000000f0)
                                break;
                        }
                        // accumulate extra zoom factor
                        yacc += zoom;
                        addr += pitch * (yacc >> 9);
                        yacc &= 0x1ff;
                        y++;
                        break;
                    }
                    case 10:
                    {
                        // shadows, clipped, count==3, not-flipped
                        uint16_t* pPix2 = pixels + ((y+1) * scrn_width) + xpos;
                        uint16_t* pPix3 = pixels + ((y+2) * scrn_width) + xpos;
                        for (int32_t x = xpos; (xdelta > 0 && x < scrn_width) || (xdelta < 0 && x >= 0); ) {
                            uint32_t pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_3row();
                            pix = (pixels >> 24) & 0xf; draw_pixel_3row();
                            pix = (pixels >> 20) & 0xf; draw_pixel_3row();
                            pix = (pixels >> 16) & 0xf; draw_pixel_3row();
                            pix = (pixels >> 12) & 0xf; draw_pixel_3row();
                            pix = (pixels >>  8) & 0xf; draw_pixel_3row();
                            pix = (pixels >>  4) & 0xf; draw_pixel_3row();
                            pix = (pixels >>  0) & 0xf; draw_pixel_3row();
                            // stop if the second-to-last pixel in the group was 0xf
                            if ((pixels & 0x000000f0) == 0x000000f0)
                                break;
                        }
                        // accumulate extra zoom factors
                        for (int i=1; i<2; i++) {
                            yacc += zoom;
                            addr += pitch * (yacc >> 9);
                            yacc &= 0x1ff;
                            y++;
                        }
                        break;
                    }
                    case 11: break; // count==4 - not used as <1%
                    case 12:
                    {
                        // shadows, not clipped, count==1, not flipped
                        //for (int32_t x = xpos; (xdelta > 0 && x < scrn_width) || (xdelta < 0 && x >= 0); ) {
                        uint32_t pixels;
                        do {
                            pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_1row_nc();
                            pix = (pixels >> 24) & 0xf; draw_pixel_1row_nc();
                            pix = (pixels >> 20) & 0xf; draw_pixel_1row_nc();
                            pix = (pixels >> 16) & 0xf; draw_pixel_1row_nc();
                            pix = (pixels >> 12) & 0xf; draw_pixel_1row_nc();
                            pix = (pixels >>  8) & 0xf; draw_pixel_1row_nc();
                            pix = (pixels >>  4) & 0xf; draw_pixel_1row_nc();
                            pix = (pixels >>  0) & 0xf; draw_pixel_1row_nc();
                            // stop if the second-to-last pixel in the group was 0xf
                        } while ((pixels & 0x000000f0) != 0x000000f0);
                        break;
                    }
                    case 13:
                    {
                        // shadows, not clipped, count==2, not flipped
                        uint16_t* pPix2 = pixels + ((y+1) * scrn_width) + xpos;
                        uint32_t pixels;
                        do {
                            pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_2row_nc();
                            pix = (pixels >> 24) & 0xf; draw_pixel_2row_nc();
                            pix = (pixels >> 20) & 0xf; draw_pixel_2row_nc();
                            pix = (pixels >> 16) & 0xf; draw_pixel_2row_nc();
                            pix = (pixels >> 12) & 0xf; draw_pixel_2row_nc();
                            pix = (pixels >>  8) & 0xf; draw_pixel_2row_nc();
                            pix = (pixels >>  4) & 0xf; draw_pixel_2row_nc();
                            pix = (pixels >>  0) & 0xf; draw_pixel_2row_nc();
                            // stop if the second-to-last pixel in the group was 0xf
                        } while ((pixels & 0x000000f0) != 0x000000f0);
                        // accumulate extra zoom factor
                        yacc += zoom;
                        addr += pitch * (yacc >> 9);
                        yacc &= 0x1ff;
                        y++;
                        break;
                    }
                    case 14:
                    {
                        // shadows, not clipped, count==3, not flipped
                        uint16_t* pPix2 = pixels + ((y+1) * scrn_width) + xpos;
                        uint16_t* pPix3 = pixels + ((y+2) * scrn_width) + xpos;
                        uint32_t pixels;
                        do {
                            pixels = spritedata[spriteaddr++];
                            uint32_t pix;
                            // draw eight pixels
                            pix = (pixels >> 28) & 0xf; draw_pixel_3row_nc();
                            pix = (pixels >> 24) & 0xf; draw_pixel_3row_nc();
                            pix = (pixels >> 20) & 0xf; draw_pixel_3row_nc();
                            pix = (pixels >> 16) & 0xf; draw_pixel_3row_nc();
                            pix = (pixels >> 12) & 0xf; draw_pixel_3row_nc();
                            pix = (pixels >>  8) & 0xf; draw_pixel_3row_nc();
                            pix = (pixels >>  4) & 0xf; draw_pixel_3row_nc();
                            pix = (pixels >>  0) & 0xf; draw_pixel_3row_nc();
                            // stop if the second-to-last pixel in the group was 0xf
                        } while ((pixels & 0x000000f0) != 0x000000f0);
                        // accumulate extra zoom factors
                        for (int i=1; i<2; i++) {
                            yacc += zoom;
                            addr += pitch * (yacc >> 9);
                            yacc &= 0x1ff;
                            y++;
                        }
                        break;
                    }
                    case 15: break; // count==4 - not used as <1%
                }
            }
            // accumulate zoom factors; if we carry into the high bit, skip an extra row
            yacc += zoom;
            addr += pitch * (yacc >> 9);
            yacc &= 0x1ff;
        }
        draw[jump_key] += std::chrono::high_resolution_clock::now() - start;
    }

/* Timing display
    std::cout << "\033[H"; // top-left
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(setup).count();
    std::cout << "\033[K"; // clear to end of line
    std::cout << std::dec << "Setup Phase: " << ms << "ns\n";
    for (int i=0; i<16; i++) {
        std::cout << "\033[K"; // clear to end of line
        ms = std::chrono::duration_cast<std::chrono::milliseconds>(draw[i]).count();
        std::cout
            << std::setw(2) << i        << "  "
            << std::setw(8) << freq[i]  << "  "
            << std::setw(9) << ms       << " ms\n";
    }
    std::cout << "\033[K"; // clear to end of line
    std::cout << std::dec << "Path 12 rep count: ";
    for (int i=0; i<6; i++) std::cout << reps[i] << ",";

    std::cout << "\033[K"; // clear to end of line
*/
}
