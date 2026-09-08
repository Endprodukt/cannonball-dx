from pathlib import Path


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, text):
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"{path}: expected one match, found {count}: {old[:100]!r}"
        )
    write(path, text.replace(old, new, 1))


# -----------------------------------------------------------------------------
# Menu
# video.hires remains the persistent integer. Existing 0/1 values keep their
# exact historical meaning; 2 and 3 extend that to 3x and 4x.
# -----------------------------------------------------------------------------
menu = "src/main/frontend/menu.cpp"
replace_once(
    menu,
    '    const char* PIXEL_SCALER_LABEL = "PIXEL SCALER ";\n',
    '    const char* PIXEL_SCALER_LABEL = "PIXEL SCALER ";\n'
    '    const char* ENGINE_RESOLUTION_LABEL = "ENGINE RESOLUTION ";\n',
)

replace_once(
    menu,
    """    std::string pixel_scaler_menu_text()
    {
        return std::string(PIXEL_SCALER_LABEL) +
            pixel_scaler::name(
                pixel_scaler::mode.load(std::memory_order_relaxed));
    }
""",
    """    std::string pixel_scaler_menu_text()
    {
        return std::string(PIXEL_SCALER_LABEL) +
            pixel_scaler::name(
                pixel_scaler::mode.load(std::memory_order_relaxed));
    }

    int engine_resolution_scale()
    {
        return std::clamp(config.video.hires + 1, 1, 4);
    }

    std::string engine_resolution_menu_text(int scale = -1)
    {
        if (scale < 1)
            scale = engine_resolution_scale();

        const std::string value =
            scale == 1 ? "ORIGINAL" : std::to_string(scale) + "X";
        return std::string(ENGINE_RESOLUTION_LABEL) + value;
    }

    void request_engine_resolution_scale(int scale)
    {
        scale = std::clamp(scale, 1, 4);

        // Backward compatible storage:
        // video.hires 0/1/2/3 means render scale 1x/2x/3x/4x.
        config.video.hires_next = scale - 1;
        if (scale == 1)
            config.video.hiresprites = 0;

        config.videoRestartRequired = true;
    }
""",
)

replace_once(
    menu,
    """    // Keep the Ferrari detail correction visible in Enhancements. OFF is the
""",
    """    // Replace the inherited binary ORIGINAL/HI-RES entry with a DX multi-step
    // render scale. A different prefix prevents MenuBase from applying its old
    // XOR toggle when this row is activated.
    if (!menu_enhancements.empty())
    {
        auto resolution_entry = std::find_if(
            menu_enhancements.begin(),
            menu_enhancements.end(),
            [](const std::string& entry)
            {
                return starts_with_label(entry, ENTRY_HIRES) ||
                       starts_with_label(entry, ENGINE_RESOLUTION_LABEL);
            });

        if (resolution_entry != menu_enhancements.end())
            *resolution_entry = engine_resolution_menu_text();
    }

    // Keep the Ferrari detail correction visible in Enhancements. OFF is the
""",
)

replace_once(
    menu,
    """    // LEFT/RIGHT set this boolean directly like the other DX value options.
""",
    """    // Engine resolution is a four-step value. Use the existing preserve-state
    // video restart so changing it in the menu does not reset the running S16 state.
    if (state == STATE_MENU &&
        menu_selected == &menu_enhancements &&
        cursor >= 0 &&
        cursor < static_cast<int>(menu_enhancements.size()) &&
        starts_with_label(menu_enhancements[cursor], ENGINE_RESOLUTION_LABEL) &&
        (input.has_pressed(Input::LEFT) || input.has_pressed(Input::RIGHT)))
    {
        int scale = engine_resolution_scale();
        if (input.has_pressed(Input::RIGHT))
            scale = scale == 4 ? 1 : scale + 1;
        else
            scale = scale == 1 ? 4 : scale - 1;

        request_engine_resolution_scale(scale);
        menu_enhancements[cursor] = engine_resolution_menu_text(scale);
        config_save_pending = true;
        osoundint.queue_sound(sound::BEEP1);
    }

    // LEFT/RIGHT set this boolean directly like the other DX value options.
""",
)

replace_once(
    menu,
    """        if (starts_with_label(option, FERRARI_MIRROR_FIX_LABEL))
        {
""",
    """        if (starts_with_label(option, ENGINE_RESOLUTION_LABEL))
        {
            int scale = engine_resolution_scale() + 1;
            if (scale > 4)
                scale = 1;

            request_engine_resolution_scale(scale);
            menu_enhancements[cursor] = engine_resolution_menu_text(scale);
            return false;
        }

        if (starts_with_label(option, FERRARI_MIRROR_FIX_LABEL))
        {
""",
)


# -----------------------------------------------------------------------------
# Video target
# -----------------------------------------------------------------------------
video = "src/main/video.cpp"
replace_once(
    video,
    "#include <bit>          // std::byteswap (C++20/23)\n",
    "#include <bit>          // std::byteswap (C++20/23)\n"
    "#include <algorithm>    // std::clamp\n",
)
replace_once(
    video,
    """    // Internal video buffer is doubled in hi-res mode.
    if (settings->hires)
    {
        config.s16_width  <<= 1;
        config.s16_height <<= 1;
    }
""",
    """    // DX: video.hires is a backward-compatible render-scale index.
    // 0 = original 1x, 1 = existing 2x, 2 = 3x, 3 = 4x.
    const int render_scale = std::clamp(settings->hires + 1, 1, 4);
    config.s16_width  *= render_scale;
    config.s16_height *= render_scale;
""",
)


# -----------------------------------------------------------------------------
# Tiles/text
# -----------------------------------------------------------------------------
tiles = "src/main/hwvideo/hwtiles.cpp"
replace_once(
    tiles,
    "#include <cstring> // memcpy\n",
    "#include <cstring> // memcpy\n#include <algorithm> // std::fill_n, std::clamp\n",
)
replace_once(
    tiles,
    """    if (hires)
    {
        s16_width_noscale = config.s16_width >> 1;
        render8x8_tile_mask      = &hwtiles::render8x8_tile_mask_hires;
        render8x8_tile_mask_clip = &hwtiles::render8x8_tile_mask_clip_hires;
    }
""",
    """    if (hires)
    {
        const int render_scale = std::clamp(config.video.hires + 1, 1, 4);
        s16_width_noscale = config.s16_width / render_scale;
        render8x8_tile_mask      = &hwtiles::render8x8_tile_mask_hires;
        render8x8_tile_mask_clip = &hwtiles::render8x8_tile_mask_clip_hires;
    }
""",
)
replace_once(
    tiles,
    "        uint16_t row_copy[S16_WIDTH_ULTRAWIDE * 2];\n",
    "        uint16_t row_copy[S16_WIDTH_ULTRAWIDE * 4];\n",
)

text = read(tiles)
marker = "// Hires Mode: Set 4 pixels instead of one.\n"
pos = text.find(marker)
if pos < 0:
    raise SystemExit("hwtiles.cpp: hires marker not found")
text = text[:pos] + r'''// Hi-res mode: paint one logical System 16 pixel as an NxN block.
inline void set_pixel_scaled(uint16_t* buf, uint16_t data, int width, int scale)
{
    for (int row = 0; row < scale; ++row)
        std::fill_n(buf + (row * width), scale, data);
}

void hwtiles::render8x8_tile_mask_hires(
    uint16_t* buf,
    uint16_t nTileNumber,
    uint16_t StartX,
    uint16_t StartY,
    uint16_t nTilePalette,
    uint16_t nColourDepth,
    uint16_t nMaskColour,
    uint16_t nPaletteOffset)
{
    const int render_scale = std::clamp(config.video.hires + 1, 1, 4);
    const int width = config.s16_width;
    const uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = tiles + (nTileNumber << 3);

    uint16_t* tile_origin =
        buf + (StartY * render_scale * width) + (StartX * render_scale);

    for (int y = 0; y < 8; ++y)
    {
        const uint32_t p0 = *pTileData++;
        if (p0 == nMaskColour)
            continue;

        const uint32_t colours[8] =
        {
            (p0 >> 28) & 0xf,
            (p0 >> 24) & 0xf,
            (p0 >> 20) & 0xf,
            (p0 >> 16) & 0xf,
            (p0 >> 12) & 0xf,
            (p0 >>  8) & 0xf,
            (p0 >>  4) & 0xf,
             p0        & 0xf,
        };

        uint16_t* row = tile_origin + (y * render_scale * width);
        for (int x = 0; x < 8; ++x)
        {
            if (colours[x])
            {
                set_pixel_scaled(
                    row + (x * render_scale),
                    static_cast<uint16_t>(nPalette + colours[x]),
                    width,
                    render_scale);
            }
        }
    }
}

void hwtiles::render8x8_tile_mask_clip_hires(
    uint16_t* buf,
    uint16_t nTileNumber,
    int16_t StartX,
    int16_t StartY,
    uint16_t nTilePalette,
    uint16_t nColourDepth,
    uint16_t nMaskColour,
    uint16_t nPaletteOffset)
{
    const int render_scale = std::clamp(config.video.hires + 1, 1, 4);
    const int width = config.s16_width;
    const uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = tiles + (nTileNumber << 3);

    for (int y = 0; y < 8; ++y)
    {
        const int logical_y = StartY + y;
        const uint32_t p0 = *pTileData++;

        if (logical_y < 0 || logical_y >= S16_HEIGHT || p0 == nMaskColour)
            continue;

        const uint32_t colours[8] =
        {
            (p0 >> 28) & 0xf,
            (p0 >> 24) & 0xf,
            (p0 >> 20) & 0xf,
            (p0 >> 16) & 0xf,
            (p0 >> 12) & 0xf,
            (p0 >>  8) & 0xf,
            (p0 >>  4) & 0xf,
             p0        & 0xf,
        };

        for (int x = 0; x < 8; ++x)
        {
            const int logical_x = StartX + x;
            if (!colours[x] || logical_x < 0 || logical_x >= s16_width_noscale)
                continue;

            uint16_t* dest =
                buf + (logical_y * render_scale * width) +
                (logical_x * render_scale);
            set_pixel_scaled(
                dest,
                static_cast<uint16_t>(nPalette + colours[x]),
                width,
                render_scale);
        }
    }
}
'''
write(tiles, text)


# -----------------------------------------------------------------------------
# Road
# -----------------------------------------------------------------------------
road = "src/main/hwvideo/hwroad.cpp"
text = read(road)
marker = "// High Resolution (Double Resolution) Road Rendering\n"
pos = text.find(marker)
if pos < 0:
    raise SystemExit("hwroad.cpp: high-resolution marker not found")
text = text[:pos] + r'''// High Resolution Road Rendering
// Supports integer internal render scales from 2x through 4x.
// ------------------------------------------------------------------------------------------------
void HWRoad::render_background_hires(uint16_t* pixels)
{
    const int render_scale = std::clamp(config.video.hires + 1, 2, 4);
    const int width = config.s16_width;
    const uint16_t* roadram = ramBuff;

    for (int yy = 0; yy < S16_HEIGHT; ++yy)
    {
        const int data0 = roadram[0x000 + yy];
        const int data1 = roadram[0x100 + yy];
        int color = -1;

        switch (road_control & 3)
        {
            case 0:
                if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 1:
                if (data0 & 0x800) color = data0 & 0x7f;
                else if (data1 & 0x800) color = data1 & 0x7f;
                break;
            case 2:
                if (data1 & 0x800) color = data1 & 0x7f;
                else if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 3:
                if (data1 & 0x800) color = data1 & 0x7f;
                break;
        }

        if (color == -1)
            continue;

        const uint16_t c = static_cast<uint16_t>(color | color_offset3);
        const int first_y = yy * render_scale;
        for (int sub = 0; sub < render_scale; ++sub)
            std::fill_n(pixels + ((first_y + sub) * width), width, c);
    }
}

namespace
{
    inline int interpolate_wrapped(
        int current,
        int next,
        int fraction,
        int scale,
        int mask)
    {
        const int period = mask + 1;
        const int half = period >> 1;
        int delta = next - current;
        if (delta > half) delta -= period;
        else if (delta < -half) delta += period;
        return (current + (delta * fraction) / scale) & mask;
    }
}

// ------------------------------------------------------------------------------------------------
// Render Road Foreground - High Resolution Version
// Intermediate output scanlines interpolate the source road line and horizontal
// position between adjacent native System 16 scanlines.
// ------------------------------------------------------------------------------------------------
void HWRoad::render_foreground_hires(uint16_t* pixels)
{
    const int render_scale = std::clamp(config.video.hires + 1, 2, 4);
    const int width = config.s16_width;
    const int logical_width = width / render_scale;
    uint16_t* roadram = ramBuff;

    static const uint8_t priority_map[2][8] =
    {
        { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
        { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
    };
    static const ALIGN64 uint8_t priority_lookup[8][8] =
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

    for (int y = 0; y < config.s16_height; ++y)
    {
        const int yy = y / render_scale;
        const int sub = y % render_scale;

        const uint32_t data0 = roadram[0x000 + yy];
        const uint32_t data1 = roadram[0x100 + yy];

        if ((data0 & 0x800) && (data1 & 0x800))
            continue;

        int hpos0 = roadram[0x200 +
            (((road_control & 4) != 0) ? yy : (data0 & 0x1ff))] & 0xfff;
        int hpos1 = roadram[0x400 +
            (((road_control & 4) != 0) ? (0x100 + yy) : (data1 & 0x1ff))] & 0xfff;

        uint8_t* src0 = (data0 & 0x800)
            ? roads + 256 * 2 * 512
            : roads + (0x000 + ((data0 >> 1) & 0xff)) * 512;
        uint8_t* src1 = (data1 & 0x800)
            ? roads + 256 * 2 * 512
            : roads + (0x100 + ((data1 >> 1) & 0xff)) * 512;

        if (sub != 0 && yy < S16_HEIGHT - 1)
        {
            const uint32_t next0 = roadram[0x000 + yy + 1];
            const uint32_t next1 = roadram[0x100 + yy + 1];

            if (!(data0 & 0x800) && !(next0 & 0x800))
            {
                const int line = interpolate_wrapped(
                    (data0 >> 1) & 0xff,
                    (next0 >> 1) & 0xff,
                    sub,
                    render_scale,
                    0xff);
                src0 = roads + (0x000 + line) * 512;

                const int next_hpos = roadram[0x200 +
                    (((road_control & 4) != 0) ? yy + 1 : (next0 & 0x1ff))] & 0xfff;
                hpos0 = interpolate_wrapped(
                    hpos0, next_hpos, sub, render_scale, 0xfff);
            }

            if (!(data1 & 0x800) && !(next1 & 0x800))
            {
                const int line = interpolate_wrapped(
                    (data1 >> 1) & 0xff,
                    (next1 >> 1) & 0xff,
                    sub,
                    render_scale,
                    0xff);
                src1 = roads + (0x100 + line) * 512;

                const int next_hpos = roadram[0x400 +
                    (((road_control & 4) != 0)
                        ? (0x100 + yy + 1)
                        : (next1 & 0x1ff))] & 0xfff;
                hpos1 = interpolate_wrapped(
                    hpos1, next_hpos, sub, render_scale, 0xfff);
            }
        }

        uint16_t color_table[32]{};
        const int color0 = roadram[0x600 +
            (((road_control & 4) != 0) ? yy : (data0 & 0x1ff))];
        const int color1 = roadram[0x600 +
            (((road_control & 4) != 0) ? (0x100 + yy) : (data1 & 0x1ff))];
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
        if ((control == 0 && (data0 & 0x800)) ||
            (control == 3 && (data1 & 0x800)))
        {
            continue;
        }

        const int s16_x = 0x5f8 + config.s16_x_off;
        int h0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
        int h1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
        uint16_t* out = pixels + (y * width);

        for (int x = 0; x < logical_width; ++x)
        {
            const unsigned pix0 = h0 < 0x200 ? src0[h0] : 3u;
            const unsigned pix1 = h1 < 0x200 ? src1[h1] : 3u;
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

            std::fill_n(out + (x * render_scale), render_scale, colour);
            h0 = (h0 + 1) & 0xfff;
            h1 = (h1 + 1) & 0xfff;
        }
    }
}
'''
write(road, text)


# -----------------------------------------------------------------------------
# Sprites
# Keep the tuned old path for 1x/2x. At 3x/4x, render one destination scanline
# per pass so the old three-row batching limit cannot truncate vertical scale.
# -----------------------------------------------------------------------------
sprites = "src/main/hwvideo/hwsprites.cpp"
replace_once(
    sprites,
    "#include <chrono>\n",
    "#include <chrono>\n#include <algorithm>\n",
)
replace_once(
    sprites,
    """        if (config.video.hires)
        {
            x1 <<= 1;
            x2 <<= 1;
        }
""",
    """        const int render_scale = std::clamp(config.video.hires + 1, 1, 4);
        x1 *= render_scale;
        x2 *= render_scale;
""",
)
replace_once(
    sprites,
    """    static uint32_t reps[6] = {0,0,0,0,0,0};
""",
    """    const int render_scale = std::clamp(config.video.hires + 1, 1, 4);

    static uint32_t reps[6] = {0,0,0,0,0,0};
""",
)
replace_once(
    sprites,
    """        // Adjust for hi-res mode
        if (config.video.hires) {
            xpos <<= 1;
            top <<= 1;
            ytarget <<= 1;
            zoom >>= 1;
        }
""",
    """        // Scale coordinates in the actual internal render target. Dividing
        // zoom by the same factor makes the fixed-point sprite sampler emit
        // proportionally more destination pixels and gives 3x/4x finer motion.
        if (render_scale > 1) {
            xpos *= render_scale;
            top *= render_scale;
            ytarget *= render_scale;
            zoom /= render_scale;
            if (zoom < 1) zoom = 1;
        }
""",
)
replace_once(
    sprites,
    "            xpos  -= (width << (config.video.hires ? 1 : 0));   // move draw position left by rendered width\n",
    "            xpos  -= width * render_scale;   // move draw position left by rendered width\n",
)
replace_once(
    sprites,
    """        if (config.video.hiresprites == 1)
            xpos += offset;
""",
    """        if (config.video.hiresprites == 1 && render_scale > 1)
            xpos += (offset * render_scale) / 2;
""",
)

generic_sprite_path = r'''
        // The original optimized renderer batches at most three identical
        // destination rows. 3x/4x can require more, so render one row per pass.
        if (render_scale > 2)
        {
            for (y = top; y != ytarget; y += ydelta)
            {
                if (y >= 0 && y < config.s16_height)
                {
                    uint16_t* pPix1 = pixels + (y * scrn_width) + xpos;
                    uint32_t spriteaddr = addr;
                    int32_t xacc = 0;
                    const bool shadowfound =
                        shadow && (spriterom_shadowinfo[addr] == 0x11);

                    if (!shadowfound)
                    {
                        if (clip)
                        {
                            for (int32_t x = xpos;
                                 (xdelta > 0 && x < scrn_width) ||
                                 (xdelta < 0 && x >= 0); )
                            {
                                const uint32_t word = spritedata[spriteaddr++];
                                uint32_t pix;
                                pix = (word >> 28) & 0xf; draw_pixel_1row_ns();
                                pix = (word >> 24) & 0xf; draw_pixel_1row_ns();
                                pix = (word >> 20) & 0xf; draw_pixel_1row_ns();
                                pix = (word >> 16) & 0xf; draw_pixel_1row_ns();
                                pix = (word >> 12) & 0xf; draw_pixel_1row_ns();
                                pix = (word >>  8) & 0xf; draw_pixel_1row_ns();
                                pix = (word >>  4) & 0xf; draw_pixel_1row_ns();
                                pix =  word        & 0xf; draw_pixel_1row_ns();
                                if ((word & 0x000000f0) == 0x000000f0)
                                    break;
                            }
                        }
                        else
                        {
                            uint32_t word;
                            do
                            {
                                word = spritedata[spriteaddr++];
                                uint32_t pix;
                                pix = (word >> 28) & 0xf; draw_pixel_1row_nc_ns();
                                pix = (word >> 24) & 0xf; draw_pixel_1row_nc_ns();
                                pix = (word >> 20) & 0xf; draw_pixel_1row_nc_ns();
                                pix = (word >> 16) & 0xf; draw_pixel_1row_nc_ns();
                                pix = (word >> 12) & 0xf; draw_pixel_1row_nc_ns();
                                pix = (word >>  8) & 0xf; draw_pixel_1row_nc_ns();
                                pix = (word >>  4) & 0xf; draw_pixel_1row_nc_ns();
                                pix =  word        & 0xf; draw_pixel_1row_nc_ns();
                            }
                            while ((word & 0x000000f0) != 0x000000f0);
                        }
                    }
                    else
                    {
                        if (clip)
                        {
                            for (int32_t x = xpos;
                                 (xdelta > 0 && x < scrn_width) ||
                                 (xdelta < 0 && x >= 0); )
                            {
                                const uint32_t word = spritedata[spriteaddr++];
                                uint32_t pix;
                                pix = (word >> 28) & 0xf; draw_pixel_1row();
                                pix = (word >> 24) & 0xf; draw_pixel_1row();
                                pix = (word >> 20) & 0xf; draw_pixel_1row();
                                pix = (word >> 16) & 0xf; draw_pixel_1row();
                                pix = (word >> 12) & 0xf; draw_pixel_1row();
                                pix = (word >>  8) & 0xf; draw_pixel_1row();
                                pix = (word >>  4) & 0xf; draw_pixel_1row();
                                pix =  word        & 0xf; draw_pixel_1row();
                                if ((word & 0x000000f0) == 0x000000f0)
                                    break;
                            }
                        }
                        else
                        {
                            uint32_t word;
                            do
                            {
                                word = spritedata[spriteaddr++];
                                uint32_t pix;
                                pix = (word >> 28) & 0xf; draw_pixel_1row_nc();
                                pix = (word >> 24) & 0xf; draw_pixel_1row_nc();
                                pix = (word >> 20) & 0xf; draw_pixel_1row_nc();
                                pix = (word >> 16) & 0xf; draw_pixel_1row_nc();
                                pix = (word >> 12) & 0xf; draw_pixel_1row_nc();
                                pix = (word >>  8) & 0xf; draw_pixel_1row_nc();
                                pix = (word >>  4) & 0xf; draw_pixel_1row_nc();
                                pix =  word        & 0xf; draw_pixel_1row_nc();
                            }
                            while ((word & 0x000000f0) != 0x000000f0);
                        }
                    }
                }

                yacc += zoom;
                addr += pitch * (yacc >> 9);
                yacc &= 0x1ff;
            }
            continue;
        }

'''
replace_once(
    sprites,
    """        const uint16_t scrn_width = config.s16_width;
        const unsigned span = (unsigned)(x2 - x1);

//        setup += std::chrono::high_resolution_clock::now() - start;
""",
    """        const uint16_t scrn_width = config.s16_width;
        const unsigned span = (unsigned)(x2 - x1);
"""
    + generic_sprite_path
    + """//        setup += std::chrono::high_resolution_clock::now() - start;
""",
)


# -----------------------------------------------------------------------------
# Audit remaining assumptions. These lines are intentionally printed into CI
# logs so we can distinguish harmless boolean checks from old hard-coded 2x
# coordinate math before merging anything.
# -----------------------------------------------------------------------------
print("\nRemaining video.hires/settings->hires references:")
for path in Path("src/main").rglob("*"):
    if path.suffix not in (".cpp", ".hpp", ".h"):
        continue
    for no, line in enumerate(
        path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1
    ):
        if "video.hires" in line or "settings->hires" in line:
            print(f"{path}:{no}: {line.strip()}")
