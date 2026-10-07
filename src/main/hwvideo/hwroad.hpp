#pragma once

#include "stdint.hpp"

class HWRoad
{
public:
    HWRoad();
    ~HWRoad();

    void init(const uint8_t*, const bool hires);
    inline void write16(uint32_t adr, const uint16_t data) {
        ram[(adr >> 1) & 0x7FF] = data;
    };
    inline void write16(uint32_t* adr, const uint16_t data) {
        uint32_t a = *adr;
        ram[(a >> 1) & 0x7FF] = data;
        *adr += 2;
    };
    inline void write32(uint32_t* adr, const uint32_t data) {
        uint32_t a = (*adr) >> 1;
        ram[a & 0x7FF] = data >> 16;
        ram[(a + 1) & 0x7FF] = data & 0xFFFF;
        *adr += 4;
    };
    uint16_t read_road_control();
    void write_road_control(const uint8_t);
    void (HWRoad::*render_background)(uint16_t*);
    void (HWRoad::*render_foreground)(uint16_t*);

    // DX: colour transitions of every decoded road ROM row. The smooth hi-res
    // renderer interpolates these edge positions between the ROM rows of two
    // adjacent native scanlines instead of switching rows in whole steps.
    static const int ROW_MAX_EDGES = 48;
    struct RowEdges
    {
        int16_t  count;      // -1 = too many transitions, row is used unsmoothed
        uint8_t  first_code;
        uint16_t pos[ROW_MAX_EDGES];  // x where the colour changes to code[i]
        uint8_t  code[ROW_MAX_EDGES];
    };

private:
    uint8_t road_control;
    uint16_t color_offset1;
    uint16_t color_offset2;
    uint16_t color_offset3;
    int32_t x_offset;

    static const uint16_t ROAD_RAM_SIZE = 0x1000;
    static const uint16_t rom_size = 0x8000;

    // Decoded road graphics
    uint8_t roads[0x40200];

    // Two halves of RAM
    uint16_t ram[ROAD_RAM_SIZE / 2];
    uint16_t ramBuff[ROAD_RAM_SIZE / 2];

    RowEdges row_edges[256 * 2 + 1];

    void decode_road(const uint8_t*);
    void build_row_edges();
    void build_hires_road_line(int road, int yy, int sub, int render_scale,
                               uint8_t* vrow, int& hpos_fp);
    void render_background_lores(uint16_t*);
    void render_foreground_lores(uint16_t*);
    void render_background_hires(uint16_t*);
    void render_foreground_hires(uint16_t*);
    void render_foreground_hires_smooth(uint16_t*);
};

extern HWRoad hwroad;
