from pathlib import Path


path = Path("src/main/sdl2/rendersurface.cpp")
text = path.read_text(encoding="utf-8")
changed = False

# ---------------------------------------------------------------------------
# Blargg width normalisation
#
# The stock low-res blitter consumes 3 source pixels for 7 output pixels.
# CannonBall-SE's hires blitter deliberately consumes twice as many source
# pixels (6) for the same 7 outputs. That makes 1x and 2x have the same
# effective analogue bandwidth. Feeding 3x/4x directly to that 2x blitter
# makes the filter progressively weaker and, on the SIMD path, violates the
# end-of-row width assumptions.
#
# Keep the high-resolution engine framebuffer, but normalise the horizontal
# input presented to Blargg to the established 2x sampling grid. Vertical
# resolution remains at the selected engine scale.
# ---------------------------------------------------------------------------
init_start = text.index("void RenderSurface::init_blargg_filter()")
calc_start = text.index("    if (blargg) {", init_start)
config_anchor = text.index("        // configure selcted filtering type", calc_start)

if "Blargg always sees the established 2x horizontal sampling grid" not in text[init_start:config_anchor]:
    new_calc = '''    if (blargg) {\n        // Blargg always sees the established 2x horizontal sampling grid for\n        // every engine resolution above original. The hires NTSC blitter is\n        // explicitly a 2x-input blitter (6 input pixels -> 7 outputs), so 3x\n        // and 4x are resampled horizontally before filtering rather than being\n        // passed to it as if they were native 2x inputs.\n        const int render_scale = std::clamp(config.video.hires + 1, 1, 4);\n        const int filter_input_width =\n            render_scale > 1 ? (src_width / render_scale) * 2 : src_width;\n\n        // first calculate the resultant image size.\n        if (render_scale > 1) {\n            #if SNES_NTSC_HAVE_SIMD\n                // The supported S16 widths normalise to the same SIMD-safe 2x\n                // widths used by CannonBall-SE: 640, 808 and 1072 pixels.\n                snes_src_width = SNES_NTSC_OUT_WIDTH_SIMD(filter_input_width);\n            #else\n                snes_src_width = SNES_NTSC_OUT_WIDTH((filter_input_width >> 1));\n                unsigned check_width = SNES_NTSC_IN_WIDTH(snes_src_width);\n                while (check_width < (filter_input_width >> 1))\n                    check_width = SNES_NTSC_IN_WIDTH(++snes_src_width);\n            #endif\n        } else {\n            snes_src_width = SNES_NTSC_OUT_WIDTH(filter_input_width);\n            unsigned check_width = SNES_NTSC_IN_WIDTH(snes_src_width);\n            while (check_width < filter_input_width)\n                check_width = SNES_NTSC_IN_WIDTH(++snes_src_width);\n        }\n\n'''
    text = text[:calc_start] + new_calc + text[config_anchor:]
    changed = True
    print("Blargg: normalised 3x/4x to the established 2x horizontal input grid")
else:
    print("Blargg width normalisation already applied")


# Replace the complete filtering function. The new path keeps 1x and 2x bit-for-
# bit on their existing sampling grids, while 3x/4x select two representative
# subpixels per original System 16 pixel before entering the hires NTSC blitter.
filter_start = text.index("void RenderSurface::blargg_filter(uint16_t* gamePixels")
filter_end = text.index("\n\n\n#include <stdint.h>", filter_start)
old_filter = text[filter_start:filter_end]

if "const int filter_input_width" not in old_filter:
    new_filter = r'''void RenderSurface::blargg_filter(uint16_t* gamePixels, uint32_t* outputPixels, int section)
{
    // Blargg is fundamentally a horizontal analogue-video filter. The SE
    // hires blitter models exactly a 2x source: six high-resolution samples
    // feed the same seven output samples that the original-res path produces
    // from three. For 3x/4x engine rendering, preserve every vertical row but
    // resample each horizontal row onto that proven 2x grid first.
    const int render_scale = std::clamp(config.video.hires + 1, 1, 4);
    const int filter_input_width =
        render_scale > 1 ? (src_width / render_scale) * 2 : src_width;

    const int block_height = section >= 0 ? (src_height >> 1) : src_height;
    const int start_y = section == 1 ? (src_height >> 1) : 0;

    if (!blargg)
        return;

    for (int row = 0; row < block_height; ++row)
    {
        const int y = start_y + row;
        const uint16_t* src_row = gamePixels +
            static_cast<size_t>(y) * static_cast<size_t>(src_width);
        uint16_t* dst_row = rgb_pixels +
            static_cast<size_t>(y) * static_cast<size_t>(filter_input_width);

        if (render_scale <= 2)
        {
            // Preserve the original CannonBall-SE 1x/2x conversion exactly.
            for (int x = 0; x < filter_input_width; ++x)
                dst_row[x] = rgb_blargg[src_row[x]];
        }
        else
        {
            // Sample at the centre of each destination subpixel. This converts
            // 3x/4x to the same two samples per native System 16 pixel expected
            // by the hires Blargg path while retaining the high-res geometry
            // before the analogue filter is applied.
            for (int x = 0; x < filter_input_width; ++x)
            {
                int source_x = ((2 * x + 1) * render_scale) >> 2;
                if (source_x >= src_width)
                    source_x = src_width - 1;
                dst_row[x] = rgb_blargg[src_row[source_x]];
            }
        }
    }

    const long output_pitch = static_cast<long>(snes_src_width) << 2;
    uint16_t* filter_input = rgb_pixels +
        static_cast<size_t>(start_y) * static_cast<size_t>(filter_input_width);
    uint32_t* filter_output = outputPixels +
        static_cast<size_t>(start_y) * static_cast<size_t>(snes_src_width);
    const uint32_t Ashifted = uint32_t(Alevel);

    if (render_scale > 1)
    {
        #if SNES_NTSC_HAVE_SIMD
            snes_ntsc_blit_hires_fast(
                ntsc,
                filter_input,
                long(filter_input_width),
                phase,
                filter_input_width,
                block_height,
                filter_output,
                output_pitch,
                Ashifted);
        #else
            snes_ntsc_blit_hires(
                ntsc,
                filter_input,
                long(filter_input_width),
                phase,
                filter_input_width,
                block_height,
                filter_output,
                output_pitch,
                Ashifted);
        #endif
    }
    else
    {
        snes_ntsc_blit(
            ntsc,
            filter_input,
            long(filter_input_width),
            phase,
            filter_input_width,
            block_height,
            filter_output,
            output_pitch,
            Ashifted);
    }
}'''
    text = text[:filter_start] + new_filter + text[filter_end:]
    changed = True
    print("Blargg: replaced unsafe direct 3x/4x hires filtering")
else:
    print("Blargg filtering function already normalised")


# ---------------------------------------------------------------------------
# Resolution-independent scanline thickness
#
# At 2x, one dark row occupies 50% of each native pixel row. The previous 3x
# and 4x code still darkened only one row, reducing coverage to 33% and 25%.
# Keep approximately 50% coverage at every higher engine scale. Odd 3x cannot
# represent 1.5 rows geometrically, so use one full row plus one half-strength
# row; this avoids alternating thick/thin bars.
# ---------------------------------------------------------------------------
scan32_start = text.index("static inline void apply_scanlines(uint32_t *pixels,")
scan32_end = text.index("\n\n\n// == 16-bit ARGB1555 ==", scan32_start)
old_scan32 = text[scan32_start:scan32_end]

new_scan32 = r'''static inline uint8_t scanline_coverage_for_row(size_t y)
{
    const size_t render_scale =
        static_cast<size_t>(std::clamp(config.video.hires + 1, 1, 4));

    // Original resolution cannot represent half a source row, so retain the
    // legacy every-other-row mask.
    if (render_scale <= 1)
        return (y & 1u) ? 255u : 0u;

    const size_t subrow = y % render_scale;
    const size_t full_rows = render_scale >> 1;
    const size_t first_full_row = render_scale - full_rows;

    if (subrow >= first_full_row)
        return 255u;

    // 3x needs 1.5 dark rows to match the 50% coverage of 2x. Give the row
    // immediately above the full scanline half the normal attenuation.
    if ((render_scale & 1u) && subrow + 1 == first_full_row)
        return 128u;

    return 0u;
}

static inline void apply_scanlines(uint32_t *pixels,
                                     size_t width, size_t height,
                                     uint8_t shift,
                                     uint8_t Rshift, uint8_t Gshift, uint8_t Bshift, uint8_t Ashift,
                                     int     section)
{
    const size_t block_height = (section >= 0 ? (height >> 1) : height);
    const size_t starty = (section == 1 ? block_height : 0);
    const size_t endy   = starty + block_height;

    for (size_t y = starty; y < endy; ++y) {
        const uint8_t coverage = scanline_coverage_for_row(y);
        if (coverage == 0)
            continue;

        uint32_t *row = pixels + y * width;
        for (size_t x = 0; x < width; x++, row++) {
            uint32_t p = *row;

            uint8_t r = (p >> Rshift) & 0xFF;
            uint8_t g = (p >> Gshift) & 0xFF;
            uint8_t b = (p >> Bshift) & 0xFF;
            uint8_t a = (p >> Ashift) & 0xFF;

            const uint8_t lum = ((77 * r + 150 * g + 29 * b) >> 8);

            const uint8_t rd = r >> shift;
            const uint8_t gd = g >> shift;
            const uint8_t bd = b >> shift;

            uint8_t out_r = (rd * (255 - lum) + r * lum) >> 8;
            uint8_t out_g = (gd * (255 - lum) + g * lum) >> 8;
            uint8_t out_b = (bd * (255 - lum) + b * lum) >> 8;

            if (coverage != 255u)
            {
                out_r = static_cast<uint8_t>(
                    (out_r * coverage + r * (255u - coverage) + 127u) / 255u);
                out_g = static_cast<uint8_t>(
                    (out_g * coverage + g * (255u - coverage) + 127u) / 255u);
                out_b = static_cast<uint8_t>(
                    (out_b * coverage + b * (255u - coverage) + 127u) / 255u);
            }

            *row = (uint32_t(out_r) << Rshift)
                 | (uint32_t(out_g) << Gshift)
                 | (uint32_t(out_b) << Bshift)
                 | (uint32_t(a)     << Ashift);
        }
    }
}'''

if "scanline_coverage_for_row" not in old_scan32:
    text = text[:scan32_start] + new_scan32 + text[scan32_end:]
    changed = True
    print("Scanlines: made 32-bit thickness resolution-independent")
else:
    print("32-bit scanline coverage already updated")

# Re-locate after the first replacement.
scan16_start = text.index("static inline void apply_scanlines(uint16_t *pixels,")
scan16_end = text.index("\n\n\nstatic void apply_crt_bloom", scan16_start)
old_scan16 = text[scan16_start:scan16_end]

new_scan16 = r'''static inline void apply_scanlines(uint16_t *pixels,
                                   size_t width, size_t height,
                                   uint8_t shift,
                                   uint8_t Rshift, uint8_t Gshift, uint8_t Bshift, uint8_t Ashift,
                                   int     section)
{
    auto expand5 = [](uint32_t v5) -> uint32_t {
        return (v5 << 3) | (v5 >> 2);
    };
    auto quantize5 = [](uint32_t v8) -> uint32_t {
        return v8 >> 3;
    };

    const size_t block_height = (section >= 0 ? (height >> 1) : height);
    const size_t starty = (section == 1 ? block_height : 0);
    const size_t endy   = starty + block_height;
    const uint16_t Amask =
        (Ashift < 16) ? (uint16_t(1u) << Ashift) : 0;

    for (size_t y = starty; y < endy; ++y) {
        const uint8_t coverage = scanline_coverage_for_row(y);
        if (coverage == 0)
            continue;

        uint16_t *row = pixels + y * width;
        for (size_t x = 0; x < width; ++x, ++row) {
            const uint16_t p = *row;

            const uint32_t r5 = (p >> Rshift) & 0x1Fu;
            const uint32_t g5 = (p >> Gshift) & 0x1Fu;
            const uint32_t b5 = (p >> Bshift) & 0x1Fu;
            const uint16_t a1 = (Ashift < 16) ? (p & Amask) : 0;

            const uint32_t r8 = expand5(r5);
            const uint32_t g8 = expand5(g5);
            const uint32_t b8 = expand5(b5);
            const uint32_t lum = (77u * r8 + 150u * g8 + 29u * b8) >> 8;

            const uint32_t rd8 = r8 >> shift;
            const uint32_t gd8 = g8 >> shift;
            const uint32_t bd8 = b8 >> shift;

            uint32_t out_r8 = (rd8 * (255u - lum) + r8 * lum) >> 8;
            uint32_t out_g8 = (gd8 * (255u - lum) + g8 * lum) >> 8;
            uint32_t out_b8 = (bd8 * (255u - lum) + b8 * lum) >> 8;

            if (coverage != 255u)
            {
                out_r8 =
                    (out_r8 * coverage + r8 * (255u - coverage) + 127u) / 255u;
                out_g8 =
                    (out_g8 * coverage + g8 * (255u - coverage) + 127u) / 255u;
                out_b8 =
                    (out_b8 * coverage + b8 * (255u - coverage) + 127u) / 255u;
            }

            *row = uint16_t(
                  (quantize5(out_r8) << Rshift)
                | (quantize5(out_g8) << Gshift)
                | (quantize5(out_b8) << Bshift)
                | a1);
        }
    }
}'''

if "scanline_coverage_for_row(y)" not in old_scan16:
    text = text[:scan16_start] + new_scan16 + text[scan16_end:]
    changed = True
    print("Scanlines: made 16-bit thickness resolution-independent")
else:
    print("16-bit scanline coverage already updated")


if changed:
    path.write_text(text, encoding="utf-8")
else:
    print("No source changes required")
