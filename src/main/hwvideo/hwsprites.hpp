#pragma once

#include "stdint.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/oroad.hpp"
#include <chrono>

class video;

class hwsprites
{
public:
    hwsprites();
    ~hwsprites();
    void init(const uint8_t*);
    void reset();
    void set_x_clip(bool);
    void swap();
    uint8_t read(const uint16_t adr);
    void write(const uint16_t adr, const uint16_t data);
    void render(uint16_t* pixels, const uint8_t);

    // 21:9-only wrapper for Stage 3's rightmost route. The original cloud
    // scenery is authored for the 320-pixel 4:3 viewport, so ultrawide exposes
    // the real left/right edges of the cloud sprites. After the normal sprite
    // render, repeat the original 320-pixel sky composition into the extra
    // ultrawide margins. This keeps the cloud scale/pixel art unchanged and
    // leaves both 4:3 and 16:9 on the original renderer path.
    void render(uint16_t* pixels, int priority)
    {
        render(pixels, static_cast<uint8_t>(priority));

        if (config.video.widescreen != 2 || oroad.stage_lookup_off != 0x12)
            return;

        const int scale = config.video.hires ? 2 : 1;
        const int side_width = config.s16_x_off * scale;
        const int original_width = S16_WIDTH * scale;

        if (side_width <= 0 || original_width <= 0)
            return;

        int sky_bottom = oroad.horizon_y2 * scale;
        if (sky_bottom < 0)
            return;
        if (sky_bottom > config.s16_height)
            sky_bottom = config.s16_height;

        const int centre_left = side_width;
        const int centre_right = centre_left + original_width;

        for (int y = 0; y < sky_bottom; y++)
        {
            uint16_t* row = pixels + (y * config.s16_width);

            // Extend the original 4:3 composition to the left using the
            // opposite edge of the 320-pixel source window.
            for (int x = 0; x < side_width; x++)
            {
                const int src_x = centre_right - side_width + x;
                row[x] = row[src_x];
            }

            // And wrap the left edge of the original composition into the
            // newly visible right-hand ultrawide area.
            for (int x = 0; x < side_width; x++)
            {
                const int dst_x = centre_right + x;
                const int src_x = centre_left + x;
                row[dst_x] = row[src_x];
            }
        }
    }

    std::chrono::nanoseconds setup{}; //initialises to zero
    std::chrono::nanoseconds draw[16]{};

private:
    // Clip values.
    uint16_t x1, x2;

    // 128 sprites, 16 bytes each (0x400)
    static const uint16_t SPRITE_RAM_SIZE = 128 * 16; // was *8
    static const uint32_t SPRITES_LENGTH = 0x100000 >> 2;
    static const uint16_t COLOR_BASE = 0x800;

    uint32_t sprites[SPRITES_LENGTH];               // Little-endian forward sprites
    uint32_t sprites_flipped[SPRITES_LENGTH];       // Little-endian flipped sprites
    // sprites_shadowinfo contains, at the start address of each sprite,
    // - 0xff for each row that has not yest been processed
    // - 0x11 for each row that contains a shadow entry (0xA);
    // - 0x00 otherwise
    // This helps with rendering as we can use a lower-cost routine 90% of the time
    uint8_t sprites_shadowinfo[SPRITES_LENGTH];

    // Two halves of RAM
    uint16_t ram[SPRITE_RAM_SIZE];
    uint16_t ramBuff[SPRITE_RAM_SIZE];

};
