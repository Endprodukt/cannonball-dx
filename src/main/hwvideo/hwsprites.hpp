#pragma once

#include "stdint.hpp"
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
