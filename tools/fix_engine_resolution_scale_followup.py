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


# Pixel scalers intentionally consume the logical/native image before HQx/xBRZ.
# Sample one pixel per current engine-render-scale step, not always every 2nd.
scaler = "src/main/sdl2/pixelscaler_renderer.hpp"
replace_once(
    scaler,
    "        input_step = config.video.hires ? 2 : 1;\n",
    "        input_step = std::clamp(config.video.hires + 1, 1, 4);\n",
)


# Time Trial selector margins use native coordinates and must scale with the
# actual internal target.
ttrial = "src/main/frontend/ttrial.cpp"
replace_once(
    ttrial,
    "        const int scale = config.video.hires ? 2 : 1;\n",
    "        const int scale = std::clamp(config.video.hires + 1, 1, 4);\n",
)


# Embedded 21:9 Music Select side art: expand every logical art pixel to NxN.
side = "src/main/engine/music_side_art_base.hpp"
replace_once(
    side,
    """        const bool hires = config.video.hires != 0;
        constexpr int SIDE_WIDTH = 68;
""",
    """        const int render_scale =
            config.video.hires < 0 ? 1 :
            (config.video.hires > 3 ? 4 : config.video.hires + 1);
        constexpr int SIDE_WIDTH = 68;
""",
)
replace_once(
    side,
    """                if (!hires)
                {
                    buffer[static_cast<int>(span.y) * config.s16_width + logical_x] = pixel;
                }
                else
                {
                    const int physical_x = logical_x << 1;
                    const int physical_y = static_cast<int>(span.y) << 1;
                    uint16_t* dst =
                        buffer + physical_y * config.s16_width + physical_x;

                    dst[0] = pixel;
                    dst[1] = pixel;
                    dst[config.s16_width] = pixel;
                    dst[config.s16_width + 1] = pixel;
                }
""",
    """                const int physical_x = logical_x * render_scale;
                const int physical_y = static_cast<int>(span.y) * render_scale;
                uint16_t* dst =
                    buffer + physical_y * config.s16_width + physical_x;

                for (int sy = 0; sy < render_scale; ++sy)
                {
                    uint16_t* row = dst + sy * config.s16_width;
                    for (int sx = 0; sx < render_scale; ++sx)
                        row[sx] = pixel;
                }
""",
)

corrections = "src/main/engine/music_side_art_corrections.hpp"
replace_once(
    corrections,
    """    inline void write_pixel(uint16_t* buffer, int x, int y, uint16_t pixel)
    {
        if (!config.video.hires)
        {
            buffer[y * config.s16_width + x] = pixel;
            return;
        }

        const int physical_x = x << 1;
        const int physical_y = y << 1;
        uint16_t* dst =
            buffer + physical_y * config.s16_width + physical_x;

        dst[0] = pixel;
        dst[1] = pixel;
        dst[config.s16_width] = pixel;
        dst[config.s16_width + 1] = pixel;
    }
""",
    """    inline void write_pixel(uint16_t* buffer, int x, int y, uint16_t pixel)
    {
        const int render_scale =
            config.video.hires < 0 ? 1 :
            (config.video.hires > 3 ? 4 : config.video.hires + 1);
        const int physical_x = x * render_scale;
        const int physical_y = y * render_scale;
        uint16_t* dst =
            buffer + physical_y * config.s16_width + physical_x;

        for (int sy = 0; sy < render_scale; ++sy)
        {
            uint16_t* row = dst + sy * config.s16_width;
            for (int sx = 0; sx < render_scale; ++sx)
                row[sx] = pixel;
        }
    }
""",
)


# Blargg's special hi-res input format is intrinsically a 2x format. At 3x/4x
# it would pair the wrong source pixels and derive an invalid output width. Keep
# the engine render scale intact and bypass Blargg only for those experimental
# scales. Switching back to 1x/2x restores the configured filter on re-init.
render = "src/main/sdl2/rendersurface.cpp"
replace_once(
    render,
    """void RenderSurface::init_blargg_filter()
{
    // Initialises the Blargg NTSC filter effects. This configures the output (s-video/rgb etc) and
""",
    """void RenderSurface::init_blargg_filter()
{
    const int render_scale = std::clamp(config.video.hires + 1, 1, 4);
    if (blargg && render_scale > 2)
    {
        std::cout
            << "Blargg filter bypassed at " << render_scale
            << "x engine resolution (Blargg hi-res input is fixed at 2x)."
            << std::endl;
        blargg = video_settings_t::BLARGG_DISABLE;
    }

    // Initialises the Blargg NTSC filter effects. This configures the output (s-video/rgb etc) and
""",
)


print("Follow-up render-scale compatibility changes applied.")
