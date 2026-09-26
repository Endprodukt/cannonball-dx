#pragma once

#include <algorithm>
#include <cstdint>

// Second-stage DX high-resolution road experiment.
//
// The previous clean-profile renderer derives every depth from one reference
// ROM row. That makes the road smooth, but also changes real OutRun geometry:
// border stripes, CENT and the relationship between road 0/1 are not uniformly
// scaled copies of a single cross-section. This renderer preserves every native
// ROM row as its own geometry and interpolates corresponding edges only when the
// two neighbouring rows have exactly the same topology.
//
// Keep the two changes independently switchable so screenshots can compare all
// four combinations without touching gameplay or the low-resolution renderer.
namespace
{
    constexpr bool DX_USE_ROM_ROW_EDGES = true;
    constexpr bool DX_DEPTH_TOWARDS_ABOVE = true;

    struct DxRomRoadRow
    {
        bool valid;
        int edge_count;
        int edge_x[DX_ROAD_MAX_EDGES];
        uint8_t codes[DX_ROAD_MAX_EDGES + 1];
    };

    DxRomRoadRow dx_rom_road_rows[2][DX_ROAD_ROWS]{};
    bool dx_rom_road_rows_built[2]{};

    bool dx_verify_rom_row(const uint8_t* source, const DxRomRoadRow& row)
    {
        if (!row.valid)
            return false;

        int edge = 0;
        uint8_t code = row.codes[0];
        for (int x = 0; x < DX_ROAD_WIDTH; ++x)
        {
            while (edge < row.edge_count && x >= row.edge_x[edge])
            {
                ++edge;
                code = row.codes[edge];
            }

            if (code != source[x])
                return false;
        }
        return true;
    }

    void dx_build_rom_road_rows(const uint8_t* roads, int road_index)
    {
        if (dx_rom_road_rows_built[road_index])
            return;

        dx_rom_road_rows_built[road_index] = true;
        const int road_base = road_index * DX_ROAD_ROWS * DX_ROAD_WIDTH;

        for (int row_index = 0; row_index < DX_ROAD_ROWS; ++row_index)
        {
            DxRomRoadRow& row = dx_rom_road_rows[road_index][row_index];
            const uint8_t* source =
                roads + road_base + row_index * DX_ROAD_WIDTH;

            row.valid = true;
            row.edge_count = 0;
            row.codes[0] = source[0];

            for (int x = 1; x < DX_ROAD_WIDTH; ++x)
            {
                if (source[x] == source[x - 1])
                    continue;

                if (row.edge_count >= DX_ROAD_MAX_EDGES)
                {
                    row.valid = false;
                    break;
                }

                row.edge_x[row.edge_count] = x;
                ++row.edge_count;
                row.codes[row.edge_count] = source[x];
            }

            // Cheap one-time invariant test: rebuilding any integer ROM row from
            // its run list must reproduce roads[] pixel-for-pixel. If it does not,
            // mark only that row invalid and let the established clean renderer
            // handle it as a fallback.
            if (row.valid && !dx_verify_rom_row(source, row))
                row.valid = false;
        }
    }

    bool dx_same_topology(const DxRomRoadRow& a, const DxRomRoadRow& b)
    {
        if (!a.valid || !b.valid || a.edge_count != b.edge_count)
            return false;

        for (int i = 0; i <= a.edge_count; ++i)
        {
            if (a.codes[i] != b.codes[i])
                return false;
        }
        return true;
    }

    bool dx_copy_rom_row_profile(
        const DxRomRoadRow& row,
        int render_scale,
        DxRenderRoadProfile& output)
    {
        output.valid = false;
        if (!row.valid)
            return false;

        output.edge_count = row.edge_count;
        for (int i = 0; i <= row.edge_count; ++i)
            output.codes[i] = row.codes[i];
        for (int i = 0; i < row.edge_count; ++i)
            output.edge_scaled[i] = row.edge_x[i] * render_scale;

        output.valid = true;
        return true;
    }

    bool dx_make_rom_row_render_profile(
        const uint8_t* roads,
        int road_index,
        int selector_fp,
        int render_scale,
        DxRenderRoadProfile& output)
    {
        dx_build_rom_road_rows(roads, road_index);

        selector_fp = std::clamp(
            selector_fp,
            0,
            0x1ff * DX_DEPTH_FP_ONE);

        // Preserve the hardware convention exactly at every integer selector.
        // The road ROM row is selector >> 1, so even and odd integer selectors
        // both map pixel-identically to the same decoded ROM row. Geometry only
        // interpolates in the fractional interval where the next integer selector
        // actually changes to a different ROM row.
        const int selector0 = selector_fp / DX_DEPTH_FP_ONE;
        const int fraction = selector_fp % DX_DEPTH_FP_ONE;
        const int selector1 = std::min(selector0 + 1, 0x1ff);
        const int row0_index = std::min(selector0 >> 1, DX_ROAD_ROWS - 1);
        const int row1_index = std::min(selector1 >> 1, DX_ROAD_ROWS - 1);

        const DxRomRoadRow& row0 = dx_rom_road_rows[road_index][row0_index];
        const DxRomRoadRow& row1 = dx_rom_road_rows[road_index][row1_index];

        if (fraction == 0 || row0_index == row1_index)
            return dx_copy_rom_row_profile(row0, render_scale, output);

        if (!row0.valid || !row1.valid)
            return false;

        if (!dx_same_topology(row0, row1))
        {
            // A topology change is a real discrete ROM event. Do not match
            // unrelated edges; switch at the nearest half-way point instead.
            const DxRomRoadRow& nearest =
                (fraction * 2 < DX_DEPTH_FP_ONE) ? row0 : row1;
            return dx_copy_rom_row_profile(nearest, render_scale, output);
        }

        output.valid = false;
        output.edge_count = row0.edge_count;
        for (int i = 0; i <= row0.edge_count; ++i)
            output.codes[i] = row0.codes[i];

        int previous = 0;
        for (int i = 0; i < row0.edge_count; ++i)
        {
            const int64_t weighted =
                static_cast<int64_t>(row0.edge_x[i]) *
                    (DX_DEPTH_FP_ONE - fraction) +
                static_cast<int64_t>(row1.edge_x[i]) * fraction;
            int position_scaled = dx_round_div(
                weighted * render_scale,
                DX_DEPTH_FP_ONE);
            position_scaled = std::clamp(
                position_scaled,
                0,
                DX_ROAD_WIDTH * render_scale);
            if (position_scaled < previous)
                position_scaled = previous;

            output.edge_scaled[i] = position_scaled;
            previous = position_scaled;
        }

        output.valid = true;
        return true;
    }

    bool dx_make_selected_road_profile(
        const uint8_t* roads,
        int road_index,
        int selector_fp,
        int render_scale,
        DxRenderRoadProfile& output)
    {
        if (DX_USE_ROM_ROW_EDGES &&
            dx_make_rom_row_render_profile(
                roads, road_index, selector_fp, render_scale, output))
        {
            return true;
        }

        // Retain the first clean-profile experiment as a comparison/fallback.
        return dx_make_render_road_profile(
            roads, road_index, selector_fp, render_scale, output);
    }

    int dx_inverse_depth_towards_above(
        const int16_t* curve,
        int above_selector,
        int current_selector,
        int sub,
        int render_scale)
    {
        current_selector = std::clamp(current_selector, 0, 0x1ff);
        above_selector = std::clamp(above_selector, 0, 0x1ff);

        if (render_scale <= 1 || sub < 0 || sub >= render_scale)
            return current_selector * DX_DEPTH_FP_ONE;

        // Arcade selector placement corresponds to the lower edge of the native
        // scanline. Sample the high-resolution rows between the previous native
        // selector and the current one. The last physical sub-row therefore lands
        // exactly on the arcade selector, with no implicit half-line bias.
        const int step = sub + 1;
        if (step >= render_scale)
            return current_selector * DX_DEPTH_FP_ONE;

        return dx_inverse_depth_fp(
            curve,
            above_selector,
            current_selector,
            step,
            render_scale);
    }
}

void HWRoad::select_hires_romrow_renderer()
{
    if (roadYCurveValid)
        render_foreground = &HWRoad::render_foreground_hires_romrows;
}

void HWRoad::render_foreground_hires_romrows(uint16_t* pixels)
{
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

        if (DX_DEPTH_TOWARDS_ABOVE)
        {
            if (yy > 0)
            {
                const uint32_t above0 = roadram[0x000 + yy - 1];
                const uint32_t above1 = roadram[0x100 + yy - 1];

                if (road0_visible && !(above0 & 0x800))
                {
                    selector0_fp = dx_inverse_depth_towards_above(
                        roadYCurve,
                        above0 & 0x1ff,
                        data0 & 0x1ff,
                        sub,
                        render_scale);
                }

                if (road1_visible && !(above1 & 0x800))
                {
                    selector1_fp = dx_inverse_depth_towards_above(
                        roadYCurve,
                        above1 & 0x1ff,
                        data1 & 0x1ff,
                        sub,
                        render_scale);
                }
            }
        }
        else if (yy + 1 < S16_HEIGHT)
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
            dx_make_selected_road_profile(
                roads, 0, selector0_fp, render_scale, profile0);
        if (road1_visible)
            dx_make_selected_road_profile(
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
