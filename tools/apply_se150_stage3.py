from pathlib import Path

path = Path("src/main/engine/osprites_bugfix_base.cpp")
text = path.read_text(encoding="utf-8")

start_marker = "    // determine sprite dimensions\n"
end_marker = "    // Traffic keeps the original OutRun logical size for road geometry and\n"
start = text.index(start_marker, text.index("void OSprites::do_sprite"))
end = text.index(end_marker, start)

replacement = r'''    // Use the ROM's actual WH-table geometry. The old hi-res path estimated
    // dimensions from five unrelated anchor rows and assumed each substituted
    // frame was an exact 2x rendition. SE 1.50 demonstrated that this is false
    // for many animation/strip tables and is the source of black bottom rows
    // and horizontal jumps. Keep DX's enhanced traffic-size step, but validate
    // every substituted frame before using it.
    int16_t offset = 0;
    uint32_t zoom = 0;
    uint32_t src_offsets = 0;
    const uint32_t original_zoom = ZOOM_LOOKUP[index];

    if (config.video.hiresprites == 0)
    {
        zoom = original_zoom;
        output->set_vzoom(zoom);
        output->set_hzoom(zoom);

        uint16_t lookup_mask = ZOOM_LOOKUP[index+1];
        src_offsets = input->addr + ZOOM_LOOKUP[index+2];

        uint16_t d0 = input->draw_props | (input->zoom << 8);
        const uint16_t top_bit = d0 & 0x8000;
        d0 &= 0x7FFF;

        if (top_bit == 0)
        {
            if (ZOOM_LOOKUP[index+2] != SIZE1)
            {
                lookup_mask += 0x4000;
                d0 = lookup_mask;
            }

            d0 = (d0 & 0xFF00) + roms.rom0p->read8(src_offsets + 1);
            width = roms.rom0p->read8(WH_TABLE + d0);
            d0 = (d0 & 0xFF00) + roms.rom0p->read8(src_offsets + 3);
            height = roms.rom0p->read8(WH_TABLE + d0);
        }
        else
        {
            d0 &= 0x7C00;
            uint16_t h = d0;

            d0 = (d0 & 0xFF00) + roms.rom0p->read8(src_offsets + 1);
            width = roms.rom0p->read8(WH_TABLE + d0);
            d0 &= 0xFF;
            width += d0;

            h |= roms.rom0p->read8(src_offsets + 3);
            height = roms.rom0p->read8(WH_TABLE + h);
            h &= 0xFF;
            height += h;
        }
    }
    else
    {
        const uint32_t standard_hires_zoom = ZOOM_LOOKUP_HIRES[index];
        const uint16_t standard_hires_size = ZOOM_LOOKUP_HIRES[index+2];
        const uint16_t orig_size = ZOOM_LOOKUP_HIRES[index+3];

        zoom = standard_hires_zoom;
        uint16_t size_to_use = standard_hires_size;

        // DX enhancement retained: traffic may use one further size step when
        // the 12-bit hardware zoom field still has room.
        if ((input->control & TRAFFIC_SPRITE) && size_to_use != SIZE1)
        {
            const uint32_t candidate_zoom = zoom << 1;
            const uint16_t next_size = size_to_use >= 0x0A ? (size_to_use - 0x0A) : 0;
            if (candidate_zoom <= 0x0FFF && next_size != size_to_use)
            {
                zoom = candidate_zoom;
                size_to_use = next_size;
            }
        }

        output->set_vzoom(zoom);
        output->set_hzoom(zoom);

        src_offsets = input->addr + size_to_use;
        const uint32_t size_offset = input->addr + orig_size;

        uint16_t lookup_mask = ZOOM_LOOKUP_HIRES[index+1];
        uint16_t d0 = input->draw_props | (input->zoom << 8);
        const uint16_t top_bit = d0 & 0x8000;
        d0 &= 0x7FFF;

        const int32_t n_orig = roms.rom0p->read8(size_offset + 1);
        const int32_t rows_orig = roms.rom0p->read8(size_offset + 3) + 1;

        if (top_bit == 0)
        {
            if (orig_size != SIZE1)
            {
                lookup_mask += 0x4000;
                d0 = lookup_mask;
            }

            d0 = (d0 & 0xFF00) + n_orig;
            width = roms.rom0p->read8(WH_TABLE + d0);
            d0 = (d0 & 0xFF00) + (rows_orig - 1);
            height = roms.rom0p->read8(WH_TABLE + d0);
        }
        else
        {
            d0 &= 0x7C00;
            uint16_t h = d0;

            d0 = (d0 & 0xFF00) + n_orig;
            width = roms.rom0p->read8(WH_TABLE + d0);
            d0 &= 0xFF;
            width += d0;

            h |= (rows_orig - 1);
            height = roms.rom0p->read8(WH_TABLE + h);
            h &= 0xFF;
            height += h;
        }

        auto abs_i32 = [](int32_t value) { return value < 0 ? -value : value; };

        // Validate a candidate against the original frame. This generalises
        // SE's 2x guard so DX's optional second traffic step (normally 4x the
        // original frame/zoom pair) can survive when the ROM really contains
        // the expected larger art.
        auto candidate_matches = [&](uint32_t candidate_addr, uint32_t candidate_zoom)
        {
            if (candidate_addr == size_offset)
                return true;

            const int32_t n_candidate = roms.rom0p->read8(candidate_addr + 1);
            const int32_t rows_candidate = roms.rom0p->read8(candidate_addr + 3) + 1;

            int32_t scale = 1;
            if (original_zoom > 0)
                scale = static_cast<int32_t>((candidate_zoom + (original_zoom >> 1)) / original_zoom);
            if (scale < 2)
                scale = 2;

            const int32_t row_tolerance = scale;
            const int32_t width_tolerance = 8 * scale;
            return abs_i32(rows_candidate - scale * rows_orig) <= row_tolerance &&
                   abs_i32(n_candidate - scale * n_orig) <= width_tolerance;
        };

        if (src_offsets != size_offset && !candidate_matches(src_offsets, zoom))
        {
            // If DX's extra traffic step is the part that failed validation,
            // first fall back to SE's normal one-step hi-res frame. Only fall
            // all the way back to the original frame if that is invalid too.
            const uint32_t standard_addr = input->addr + standard_hires_size;
            if (standard_addr != size_offset &&
                candidate_matches(standard_addr, standard_hires_zoom))
            {
                src_offsets = standard_addr;
                zoom = standard_hires_zoom;
                output->set_vzoom(zoom);
                output->set_hzoom(zoom);
            }
            else
            {
                src_offsets = size_offset;
                zoom = original_zoom;
                output->set_vzoom(zoom);
                output->set_hzoom(zoom);
            }
        }

        if (src_offsets != size_offset)
        {
            const int32_t n_big = roms.rom0p->read8(src_offsets + 1);
            const int32_t z_big = static_cast<int32_t>(zoom >> 1) > 0
                ? static_cast<int32_t>(zoom >> 1) : 1;
            const int32_t z_orig = static_cast<int32_t>(original_zoom >> 1) > 0
                ? static_cast<int32_t>(original_zoom >> 1) : 1;
            const int32_t w_big = (512 * n_big + z_big - 1) / z_big;
            const int32_t w_orig = (512 * n_orig + z_orig - 1) / z_orig;
            offset = static_cast<int16_t>((w_orig - w_big) / 2);
        }
    }

    // Width/height now stay in the arcade's exact logical geometry. The larger
    // source art only improves sampling detail; it must not move collision or
    // anchoring. Bumper View may still enlarge the render geometry below.
    int32_t calc_width = width;
    int32_t calc_height = height;
    if (config.video.hiresprites == 0)
        offset = 0;

    // int16_t overload writes the DX/SE hi-res centering offset to word 15.
    output->set_offset(offset);

    const bool traffic_sprite = (input->control & TRAFFIC_SPRITE) != 0;
    const bool hardware_shadow_sprite = input->shadow != 0;

'''

text = text[:start] + replacement + text[end:]

# Correct the cull/clip bounds for a centred hi-res substitution. The offset is
# stored in hi-res canvas pixels, while these bounds are in lo-res coordinates.
old_bounds = '''    // Here we need the entire value set by above routine, not just top 0x1FF mask!\n    int16_t sprite_x1 = output->get_x() + offset;\n    int16_t sprite_x2 = sprite_x1 + calc_width + offset;\n    int16_t sprite_y1 = output->get_y();\n    int16_t sprite_y2 = sprite_y1 + geometry_height;\n'''
new_bounds = '''    // Here we need the entire value set by above routine, not just top 0x1FF mask!\n    // offset centres the substituted frame; it is not a translation. Convert\n    // from hi-res canvas pixels to logical pixels and apply opposite signs to\n    // the two edges, matching SE 1.50's corrected clip/cull model.\n    const int16_t offset_lores = static_cast<int16_t>(\n        offset >= 0 ? (offset + 1) / 2 : -((-offset + 1) / 2));\n    int16_t sprite_x1 = output->get_x() + offset_lores;\n    int16_t sprite_x2 = output->get_x() + calc_width - offset_lores;\n    int16_t sprite_y1 = output->get_y();\n    int16_t sprite_y2 = sprite_y1 + geometry_height;\n'''
if text.count(old_bounds) != 1:
    raise RuntimeError(f"sprite bounds block: expected one match, found {text.count(old_bounds)}")
text = text.replace(old_bounds, new_bounds, 1)

path.write_text(text, encoding="utf-8")
print("SE 1.50 stage 3 sprite geometry port applied successfully")
