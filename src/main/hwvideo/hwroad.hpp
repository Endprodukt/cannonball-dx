#pragma once

#include "stdint.hpp"

class HWRoad
{
public:
    HWRoad();
    ~HWRoad();

    void init(const uint8_t*, const bool hires);
    inline void write16(uint32_t adr, const uint16_t data) {
        const uint32_t index = (adr >> 1) & 0x7FF;
        ram[index] = data;
        ramFrac[index] = 0;
    };
    inline void write16(uint32_t* adr, const uint16_t data) {
        const uint32_t a = *adr;
        const uint32_t index = (a >> 1) & 0x7FF;
        ram[index] = data;
        ramFrac[index] = hscroll_fraction_for_write(a, data);
        *adr += 2;
    };
    inline void write32(uint32_t* adr, const uint32_t data) {
        const uint32_t a = (*adr) >> 1;
        ram[a & 0x7FF] = data >> 16;
        ram[(a + 1) & 0x7FF] = data & 0xFFFF;
        ramFrac[a & 0x7FF] = 0;
        ramFrac[(a + 1) & 0x7FF] = 0;
        *adr += 4;
    };
    uint16_t read_road_control();
    void write_road_control(const uint8_t);
    void (HWRoad::*render_background)(uint16_t*);
    void (HWRoad::*render_foreground)(uint16_t*);

    // DX high-resolution road path. ORoad snapshots the unquantised road_y
    // curve that corresponds to the road data just blitted into Road RAM.
    void capture_hires_road_y(const int16_t* curve);
    void select_hires_depth_renderer();

    // Exact-ROM-row follow-up renderer. Kept separate from the first depth
    // experiment so the two approaches remain directly comparable.
    void select_hires_romrow_renderer();

    // Experimental DX road model. This replaces only the high-resolution
    // fractional HScroll side-buffer; integer Road RAM remains arcade-exact.
    void apply_hires_hscroll_model(const double* road0_h, const double* road1_h);

    // Road 0 scenery in the original engine is positioned from road0_h[] while
    // the DX high-resolution road is shifted by ramFracBuff. Convert that exact
    // rendered HScroll correction back to a native-pixel sprite X correction.
    // This is render-only: object world coordinates and collision stay original.
    inline int16_t hires_scenery_x_correction(uint16_t depth) const {
        if (depth >= 0x200)
            return 0;

        // HScroll is 0x654 - road_x, so its fractional correction has the
        // opposite sign to the screen-space correction needed by scenery.
        const int correction64 = -static_cast<int>(ramFracBuff[0x200 + depth]);
        if (correction64 >= 0)
            return static_cast<int16_t>((correction64 + 32) / 64);
        return static_cast<int16_t>(-((-correction64 + 32) / 64));
    }
  
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

    // Two halves of RAM. The fractional side-buffer mirrors road RAM word-for-word
    // and stores only the 1/64-pixel road_x residue used by the high-res renderer.
    uint16_t ram[ROAD_RAM_SIZE / 2];
    uint16_t ramBuff[ROAD_RAM_SIZE / 2];
    int8_t ramFrac[ROAD_RAM_SIZE / 2];
    int8_t ramFracBuff[ROAD_RAM_SIZE / 2];

    // Raw ORoad depth curve for the same frame as ramBuff. Values retain the
    // four fractional bits that do_road_data() normally discards with >> 4.
    int16_t roadYCurve[0x200];
    bool roadYCurveValid = false;

    int8_t hscroll_fraction_for_write(uint32_t address, uint16_t data) const;
    void decode_road(const uint8_t*);
    void render_background_lores(uint16_t*);
    void render_foreground_lores(uint16_t*);
    void render_background_hires(uint16_t*);
    void render_foreground_hires(uint16_t*);
    void render_foreground_hires_depth(uint16_t*);
    void render_foreground_hires_romrows(uint16_t*);
};

extern HWRoad hwroad;
