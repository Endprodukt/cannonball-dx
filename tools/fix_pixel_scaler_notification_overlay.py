from pathlib import Path

path = Path("src/main/sdl2/pixelscaler_renderer.hpp")
text = path.read_text(encoding="utf-8")
changed = False

old = '''        if (fastpass != 1 && notification_visible())\n            draw_scaler_notification(pixels);\n\n        // Serialise only scaler state/buffers. The stock SE path retains its\n'''
new = '''        const bool show_scaler_notification =\n            fastpass != 1 && notification_visible();\n\n        // Serialise only scaler state/buffers. The stock SE path retains its\n'''
if old in text:
    text = text.replace(old, new, 1)
    changed = True
    print("Notification: detached from scaler input")
elif new in text:
    print("Notification: scaler-input detachment already applied")
else:
    raise SystemExit("Notification input hook not found")

old = '''        if (!scaler_path)\n        {\n            processing_lock.unlock();\n            RenderSurface::draw_frame(pixels, fastpass);\n            return;\n        }\n'''
new = '''        if (!scaler_path)\n        {\n            // Stock rendering still draws the notification into the native S16\n            // frame. Only an active pixel scaler needs the independent overlay\n            // path below.\n            if (show_scaler_notification)\n                draw_scaler_notification(pixels);\n\n            processing_lock.unlock();\n            RenderSurface::draw_frame(pixels, fastpass);\n            return;\n        }\n'''
if old in text:
    text = text.replace(old, new, 1)
    changed = True
    print("Notification: retained stock-path drawing")
elif new in text:
    print("Notification: stock-path drawing already applied")
else:
    raise SystemExit("Stock scaler branch not found")

old = '''        apply_low_factor_detail_preserve();\n        apply_se_scanlines_after_scaler();\n        prepare_rgba_upload();\n'''
new = '''        apply_low_factor_detail_preserve();\n        apply_se_scanlines_after_scaler();\n\n        // Draw the scaler notification last, in scaler-output space. This keeps\n        // the text independent of both engine resolution and the scaler itself:\n        // xBRZ/HQx never processes the glyphs and scanlines never darken them.\n        if (show_scaler_notification)\n            draw_scaler_notification_scaled();\n\n        prepare_rgba_upload();\n'''
if old in text:
    text = text.replace(old, new, 1)
    changed = True
    print("Notification: added post-scaler overlay hook")
elif new in text:
    print("Notification: post-scaler overlay hook already applied")
else:
    raise SystemExit("Post-scaler hook anchor not found")

anchor = '''    void apply_low_factor_detail_preserve()\n    {\n'''
helper = r'''    uint32_t notification_argb(uint16_t palette_index) const
    {
        const uint16_t raw = rgb_blargg[palette_index];
        const bool shadow = (raw & 0x8000u) != 0;
        const uint32_t r5 = (raw >> 10) & 0x1Fu;
        const uint32_t g5 = (raw >> 5) & 0x1Fu;
        const uint32_t b5 = raw & 0x1Fu;
        const auto& table = shadow ? SHADOW_DAC : STANDARD_DAC;

        return 0xFF000000u |
               (table[r5] << 16) |
               (table[g5] << 8) |
               table[b5];
    }

    void draw_scaler_notification_scaled()
    {
        if (scaled_pixels.empty() || scaled_width <= 0 || scaled_height <= 0)
            return;

        const std::string text =
            std::string("PIXEL SCALER: ") +
            pixel_scaler::name(notification_mode);

        // One native UI pixel must occupy exactly one scaler block. Therefore
        // the on-screen notification size is constant for 3x/4x/5x/6x scalers
        // and is completely independent of the engine's 1x..4x resolution.
        const int ui_scale = std::max(1, factor);
        const int glyph_height = 7 * ui_scale;
        const int advance = 6 * ui_scale;
        const int padding = 2 * ui_scale;
        const int text_width =
            static_cast<int>(text.size()) * advance - ui_scale;
        const int box_width = text_width + padding * 2;
        const int box_height = glyph_height + padding * 2;
        const int box_x = std::max(0, (scaled_width - box_width) / 2);
        const int box_y = 4 * ui_scale;

        const auto [background_index, foreground_index] =
            notification_palette_indices();
        const uint32_t background = notification_argb(background_index);
        const uint32_t foreground = notification_argb(foreground_index);

        for (int y = 0; y < box_height; ++y)
        {
            const int py = box_y + y;
            if (py < 0 || py >= scaled_height)
                continue;

            uint32_t* row = scaled_pixels.data() +
                static_cast<size_t>(py) * scaled_width;
            for (int x = 0; x < box_width; ++x)
            {
                const int px = box_x + x;
                if (px >= 0 && px < scaled_width)
                    row[px] = background;
            }
        }

        int cursor_x = box_x + padding;
        const int text_y = box_y + padding;

        for (char c : text)
        {
            const auto rows = glyph(c);
            for (int row_index = 0; row_index < 7; ++row_index)
            {
                for (int col = 0; col < 5; ++col)
                {
                    if ((rows[row_index] & (1u << (4 - col))) == 0)
                        continue;

                    const int x0 = cursor_x + col * ui_scale;
                    const int y0 = text_y + row_index * ui_scale;
                    for (int sy = 0; sy < ui_scale; ++sy)
                    {
                        const int py = y0 + sy;
                        if (py < 0 || py >= scaled_height)
                            continue;

                        uint32_t* row = scaled_pixels.data() +
                            static_cast<size_t>(py) * scaled_width;
                        for (int sx = 0; sx < ui_scale; ++sx)
                        {
                            const int px = x0 + sx;
                            if (px >= 0 && px < scaled_width)
                                row[px] = foreground;
                        }
                    }
                }
            }
            cursor_x += advance;
        }
    }

'''
if helper not in text:
    if anchor not in text:
        raise SystemExit("Notification helper insertion anchor not found")
    text = text.replace(anchor, helper + anchor, 1)
    changed = True
    print("Notification: added independent scaled overlay renderer")
else:
    print("Notification: independent scaled overlay already present")

if changed:
    path.write_text(text, encoding="utf-8")
else:
    print("No changes required")
