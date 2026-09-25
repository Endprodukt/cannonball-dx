#pragma once

#include <algorithm>
#include <cstdint>

// DX high-resolution road renderer using ORoad's unquantised road_y curve.
//
// The original hardware table stores an integer 9-bit depth selector per native
// scanline. ORoad, however, first computes the mapping from ROM depth to screen Y
// with four fractional bits and only then reduces it to that selector table. This
// renderer inverts the still-unquantised curve for each physical high-resolution
// scanline. HScroll is sampled along the same reconstructed depth, so road width
// and horizontal curvature stay phase-aligned.

namespace
{
    constexpr int DX_ROAD_ROWS = 256;
    constexpr int DX_ROAD_WIDTH = 512;
    constexpr int DX_ROAD_CENTER = 256;
    constexpr int DX_ROAD_MAX_EDGES = 32;
    constexpr int DX_ROAD_WIDTH_FP_SHIFT = 4;
    constexpr int DX_ROAD_WIDTH_FP_ONE = 1 << DX_ROAD_WIDTH_FP_SHIFT;

    // Exact 9-bit hardware selector precision used by the DX renderer.
    constexpr int DX_DEPTH_FP_SHIFT = 8;
    constexpr int DX_DEPTH_FP_ONE = 1 << DX_DEPTH_FP_SHIFT;

    // HScroll side-buffer precision from the road_x patch.
    constexpr int DX_HPOS_FRAC_ONE = 64;

    struct DxCleanRoadProfile
    {
        bool built;
        bool ready;
        int edge_count;
        int edge_rel16[DX_ROAD_MAX_EDGES];
        uint8_t codes[DX_ROAD_MAX_EDGES + 1];
        int half_width16[DX_ROAD_ROWS];
        int reference_half_width16;
    };

    struct DxRenderRoadProfile
    {
        bool valid;
        int edge_count;
        int edge_scaled[DX_ROAD_MAX_EDGES];
        uint8_t codes[DX_ROAD_MAX_EDGES + 1];
    };

    struct DxRoadSpan
    {
        unsigned code;
        int run;
    };

    DxCleanRoadProfile dx_clean_road_profiles[2]{};

    inline int dx_wrap(int value, int period)
    {
        value %= period;
        if (value < 0)
            value += period;
        return value;
    }

    inline int dx_round_div(int64_t value, int divisor)
    {
        if (value >= 0)
            return static_cast<int>((value + divisor / 2) / divisor);
        return -static_cast<int>((-value + divisor / 2) / divisor);
    }

    inline int dx_measure_half_width16(const uint8_t* row)
    {
        int left = 0;
        while (left < DX_ROAD_WIDTH && (row[left] == 3 || row[left] == 7))
            ++left;

        int right = DX_ROAD_WIDTH - 1;
        while (right >= 0 && (row[right] == 3 || row[right] == 7))
            --right;

        if (left > right)
            return 0;

        const int left_half = DX_ROAD_CENTER - left;
        const int right_half = right - (DX_ROAD_CENTER - 1);
        return std::max(left_half, right_half) * DX_ROAD_WIDTH_FP_ONE;
    }

    void dx_build_clean_road_profile(const uint8_t* roads, int road_index)
    {
        DxCleanRoadProfile& profile = dx_clean_road_profiles[road_index];
        profile.built = true;
        profile.ready = false;

        int raw_half_width16[DX_ROAD_ROWS]{};
        int reference_row = 0;
        int reference_half_width16 = 0;
        const int road_base = road_index * DX_ROAD_ROWS * DX_ROAD_WIDTH;

        for (int row = 0; row < DX_ROAD_ROWS; ++row)
        {
            const uint8_t* src = roads + road_base + row * DX_ROAD_WIDTH;
            raw_half_width16[row] = dx_measure_half_width16(src);
            if (raw_half_width16[row] > reference_half_width16)
            {
                reference_half_width16 = raw_half_width16[row];
                reference_row = row;
            }
        }

        // Keep the clean-profile behaviour from the first experimental patch:
        // smooth native ROM width quantisation, then make perspective monotonic.
        for (int row = 0; row < DX_ROAD_ROWS; ++row)
        {
            int total = 0;
            int weight_total = 0;
            for (int offset = -2; offset <= 2; ++offset)
            {
                const int source_row = row + offset;
                if (source_row < 0 || source_row >= DX_ROAD_ROWS)
                    continue;

                const int weight = 3 - (offset < 0 ? -offset : offset);
                total += raw_half_width16[source_row] * weight;
                weight_total += weight;
            }
            profile.half_width16[row] = weight_total
                ? (total + weight_total / 2) / weight_total
                : 0;
        }

        for (int row = 1; row < DX_ROAD_ROWS; ++row)
        {
            if (profile.half_width16[row] < profile.half_width16[row - 1])
                profile.half_width16[row] = profile.half_width16[row - 1];
        }

        profile.edge_count = 0;
        profile.reference_half_width16 = reference_half_width16;
        if (reference_half_width16 <= 0)
            return;

        const uint8_t* reference =
            roads + road_base + reference_row * DX_ROAD_WIDTH;

        // CENT/code 7 is deliberately kept as part of the geometry. The ROM's
        // central exterior gap and CENT marking are two sides of the same shape;
        // separating them causes the thin sand-coloured seam seen in testing.
        profile.codes[0] = reference[0];
        for (int x = 1; x < DX_ROAD_WIDTH; ++x)
        {
            if (reference[x] == reference[x - 1])
                continue;

            if (profile.edge_count >= DX_ROAD_MAX_EDGES)
                return;

            profile.edge_rel16[profile.edge_count] =
                (x - DX_ROAD_CENTER) * DX_ROAD_WIDTH_FP_ONE;
            ++profile.edge_count;
            profile.codes[profile.edge_count] = reference[x];
        }

        profile.ready = profile.edge_count > 0;
    }

    bool dx_make_render_road_profile(
        const uint8_t* roads,
        int road_index,
        int selector_fp,
        int render_scale,
        DxRenderRoadProfile& output)
    {
        DxCleanRoadProfile& profile = dx_clean_road_profiles[road_index];
        if (!profile.built)
            dx_build_clean_road_profile(roads, road_index);

        output.valid = false;
        if (!profile.ready || profile.reference_half_width16 <= 0)
            return false;

        selector_fp = std::clamp(
            selector_fp,
            0,
            0x1ff * DX_DEPTH_FP_ONE);

        // The hardware selector is 9-bit while each road ROM has 256 rows:
        // ROM row = selector / 2. Keep the half-row and sub-row fractions.
        const int row_denominator = 2 * DX_DEPTH_FP_ONE;
        const int row0 = std::min(selector_fp / row_denominator, DX_ROAD_ROWS - 1);
        const int fraction = selector_fp % row_denominator;
        const int row1 = std::min(row0 + 1, DX_ROAD_ROWS - 1);

        const int64_t width_sum =
            static_cast<int64_t>(profile.half_width16[row0]) *
                (row_denominator - fraction) +
            static_cast<int64_t>(profile.half_width16[row1]) * fraction;
        const int half_width16 = static_cast<int>(
            (width_sum + row_denominator / 2) / row_denominator);

        output.edge_count = profile.edge_count;
        for (int i = 0; i <= profile.edge_count; ++i)
            output.codes[i] = profile.codes[i];

        int previous = 0;
        for (int i = 0; i < profile.edge_count; ++i)
        {
            const int64_t scaled_relative =
                static_cast<int64_t>(profile.edge_rel16[i]) * half_width16 /
                profile.reference_half_width16;
            int position16 = DX_ROAD_CENTER * DX_ROAD_WIDTH_FP_ONE +
                static_cast<int>(scaled_relative);
            position16 = std::clamp(
                position16,
                0,
                DX_ROAD_WIDTH * DX_ROAD_WIDTH_FP_ONE);

            int position_scaled =
                (position16 * render_scale + DX_ROAD_WIDTH_FP_ONE / 2) >>
                DX_ROAD_WIDTH_FP_SHIFT;
            if (position_scaled < previous)
                position_scaled = previous;

            output.edge_scaled[i] = position_scaled;
            previous = position_scaled;
        }

        output.valid = true;
        return true;
    }

    inline DxRoadSpan dx_sample_road_span(
        const DxRenderRoadProfile& profile,
        int source_scaled,
        int scaled_visible,
        int scaled_period)
    {
        if (!profile.valid)
            return { 3u, scaled_period };

        if (source_scaled >= scaled_visible)
            return { 3u, std::max(1, scaled_period - source_scaled) };

        unsigned code = profile.codes[0];
        int next = scaled_visible;

        for (int i = 0; i < profile.edge_count; ++i)
        {
            const int edge = profile.edge_scaled[i];
            if (edge > source_scaled)
            {
                next = edge;
                break;
            }
            code = profile.codes[i + 1];
        }

        return { code, std::max(1, next - source_scaled) };
    }

    // Reconstruct exact selector position for a physical high-resolution row.
    // The native selector marks the first ROM row where floor(road_y / 16)
    // advances. For normal visible geometry, invert the raw 1/16-pixel curve
    // between this scanline and the native scanline below it.
    //
    // Hills are deliberately conservative: interpolation is accepted only when
    // the two emitted native rows represent exactly one Y step and every ROM row
    // between them remains monotonic. Otherwise hidden geometry may lie between
    // the selectors, so the original selector is retained.
    int dx_inverse_depth_fp(
        const int16_t* curve,
        int current_selector,
        int below_selector,
        int sub,
        int render_scale)
    {
        current_selector = std::clamp(current_selector, 0, 0x1ff);
        below_selector = std::clamp(below_selector, 0, 0x1ff);

        const int original = current_selector * DX_DEPTH_FP_ONE;
        if (render_scale <= 1 || sub < 0 || sub >= render_scale)
            return original;

        // Moving down the screen should move toward larger ROM selectors.
        if (below_selector <= current_selector)
            return original;

        const int current_y = curve[current_selector] >> 4;
        const int below_y = curve[below_selector] >> 4;
        if (current_y != below_y + 1)
            return original;

        // Reject crests/hidden sections. On a visible interval, raw road_y must
        // stay non-increasing as the selector increases toward the camera.
        for (int r = current_selector; r < below_selector; ++r)
        {
            if (curve[r] < curve[r + 1])
                return original;
        }

        // Top-edge convention: sub=0 corresponds to the exact transition into
        // the native scanline, matching the original selector convention rather
        // than adding an implicit half-scanline bias.
        const int64_t target_num =
            static_cast<int64_t>(current_y) * 16 * render_scale -
            static_cast<int64_t>(sub) * 16;

        for (int r = current_selector; r < below_selector; ++r)
        {
            const int64_t y0 = static_cast<int64_t>(curve[r]) * render_scale;
            const int64_t y1 = static_cast<int64_t>(curve[r + 1]) * render_scale;
            if (y0 < target_num || target_num < y1)
                continue;

            const int64_t denominator = y0 - y1;
            if (denominator <= 0)
                continue;

            const int64_t numerator = y0 - target_num;
            int fraction = static_cast<int>(
                (numerator * DX_DEPTH_FP_ONE + denominator / 2) /
                denominator);
            fraction = std::clamp(fraction, 0, DX_DEPTH_FP_ONE);
            return r * DX_DEPTH_FP_ONE + fraction;
        }

        // A steep raw step can advance the hardware emission counter without the
        // requested subline existing inside this selector interval. In that rare
        // but still monotonic case, interpolate by the emitted scanline itself.
        // This is intentionally not used for hills/non-monotonic geometry.
        const int delta = below_selector - current_selector;
        return original +
            (delta * DX_DEPTH_FP_ONE * sub + render_scale / 2) /
            render_scale;
    }

    int dx_sample_hpos_scaled(
        const uint16_t* roadram,
        const int8_t* fraction_ram,
        int table_base,
        int selector_fp,
        int render_scale)
    {
        selector_fp = std::clamp(
            selector_fp,
            0,
            0x1ff * DX_DEPTH_FP_ONE);

        const int index0 = selector_fp / DX_DEPTH_FP_ONE;
        const int fraction = selector_fp % DX_DEPTH_FP_ONE;
        const int index1 = std::min(index0 + 1, 0x1ff);

        const int period64 = 0x1000 * DX_HPOS_FRAC_ONE;
        int h0 = (roadram[table_base + index0] & 0xfff) * DX_HPOS_FRAC_ONE +
            fraction_ram[table_base + index0];
        int h1 = (roadram[table_base + index1] & 0xfff) * DX_HPOS_FRAC_ONE +
            fraction_ram[table_base + index1];

        h0 = dx_wrap(h0, period64);
        h1 = dx_wrap(h1, period64);

        int delta = h1 - h0;
        const int half = period64 >> 1;
        if (delta > half)
            delta -= period64;
        else if (delta < -half)
            delta += period64;

        const int hpos64 = dx_wrap(
            h0 + dx_round_div(
                static_cast<int64_t>(delta) * fraction,
                DX_DEPTH_FP_ONE),
            period64);

        const int scaled_period = 0x1000 * render_scale;
        return dx_wrap(
            dx_round_div(
                static_cast<int64_t>(hpos64) * render_scale,
                DX_HPOS_FRAC_ONE),
            scaled_period);
    }
}

void HWRoad::capture_hires_road_y(const int16_t* curve)
{
    if (!curve)
    {
        roadYCurveValid = false;
        return;
    }

    for (int i = 0; i < 0x200; ++i)
        roadYCurve[i] = curve[i];
    roadYCurveValid = true;
}

void HWRoad::select_hires_depth_renderer()
{
    if (roadYCurveValid)
        render_foreground = &HWRoad::render_foreground_hires_depth;
}

void HWRoad::render_foreground_hires_depth(uint16_t* pixels)
{
    // The DX inverse-depth path models the indirect selector table used by
    // OutRun. Direct scanline mode is not part of this reconstruction; retain
    // the established renderer if it is ever requested.
    if (!roadYCurveValid || (road_control & 4) != 0)
    {
        render_foreground_hires(pixels);
        return;
    }

    const int render_scale = std::clamp(config.video.hires + 1, 2, 4);
    const int width = config.s16_width;
    const uint16_t* roadram = ramBuff;

    static const uint8_t priority_map[2][8] =
    {
        { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
        { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
    };
    static const uint8_t priority_lookup[8][8] =
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

    const int scaled_period = 0x1000 * render_scale;
    const int scaled_visible = DX_ROAD_WIDTH * render_scale;

    for (int y = 0; y < config.s16_height; ++y)
    {
        const int yy = y / render_scale;
        const int sub = y % render_scale;
        if (yy < 0 || yy >= S16_HEIGHT)
            continue;

        const uint32_t data0 = roadram[0x000 + yy];
        const uint32_t data1 = roadram[0x100 + yy];

        if ((data0 & 0x800) && (data1 & 0x800))
            continue;

        const bool road0_visible = (data0 & 0x800) == 0;
        const bool road1_visible = (data1 & 0x800) == 0;

        int selector0_fp = (data0 & 0x1ff) * DX_DEPTH_FP_ONE;
        int selector1_fp = (data1 & 0x1ff) * DX_DEPTH_FP_ONE;

        if (yy + 1 < S16_HEIGHT)
        {
            const uint32_t below0 = roadram[0x000 + yy + 1];
            const uint32_t below1 = roadram[0x100 + yy + 1];

            if (road0_visible && !(below0 & 0x800))
            {
                selector0_fp = dx_inverse_depth_fp(
                    roadYCurve,
                    data0 & 0x1ff,
                    below0 & 0x1ff,
                    sub,
                    render_scale);
            }

            if (road1_visible && !(below1 & 0x800))
            {
                selector1_fp = dx_inverse_depth_fp(
                    roadYCurve,
                    data1 & 0x1ff,
                    below1 & 0x1ff,
                    sub,
                    render_scale);
            }
        }

        DxRenderRoadProfile profile0{};
        DxRenderRoadProfile profile1{};
        if (road0_visible)
            dx_make_render_road_profile(
                roads, 0, selector0_fp, render_scale, profile0);
        if (road1_visible)
            dx_make_render_road_profile(
                roads, 1, selector1_fp, render_scale, profile1);

        const int hpos0_scaled = road0_visible
            ? dx_sample_hpos_scaled(
                roadram, ramFracBuff, 0x200, selector0_fp, render_scale)
            : 0;
        const int hpos1_scaled = road1_visible
            ? dx_sample_hpos_scaled(
                roadram, ramFracBuff, 0x400, selector1_fp, render_scale)
            : 0;

        uint16_t color_table[32]{};
        const int color0 = roadram[0x600 + (data0 & 0x1ff)];
        const int color1 = roadram[0x600 + (data1 & 0x1ff)];
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
        if ((control == 0 && !road0_visible) ||
            (control == 3 && !road1_visible))
        {
            continue;
        }

        const int s16_x = 0x5f8 + config.s16_x_off;
        const int screen_offset_scaled = (s16_x + x_offset) * render_scale;
        int h0_scaled = dx_wrap(
            hpos0_scaled - screen_offset_scaled,
            scaled_period);
        int h1_scaled = dx_wrap(
            hpos1_scaled - screen_offset_scaled,
            scaled_period);
        uint16_t* out = pixels + (y * width);

        int x = 0;
        while (x < width)
        {
            const DxRoadSpan span0 = road0_visible
                ? dx_sample_road_span(
                    profile0, h0_scaled, scaled_visible, scaled_period)
                : DxRoadSpan{ 3u, width - x };
            const DxRoadSpan span1 = road1_visible
                ? dx_sample_road_span(
                    profile1, h1_scaled, scaled_visible, scaled_period)
                : DxRoadSpan{ 3u, width - x };

            const unsigned pix0 = span0.code;
            const unsigned pix1 = span1.code;

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

            int run = width - x;
            if (control != 3)
                run = std::min(run, span0.run);
            if (control != 0)
                run = std::min(run, span1.run);
            run = std::max(run, 1);

            std::fill_n(out + x, run, colour);
            x += run;

            h0_scaled += run;
            if (h0_scaled >= scaled_period)
                h0_scaled -= scaled_period;
            h1_scaled += run;
            if (h1_scaled >= scaled_period)
                h1_scaled -= scaled_period;
        }
    }
}
