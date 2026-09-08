from pathlib import Path

# Keep the final Time Trial build include fix idempotent.
path = Path("src/main/frontend/ttrial.cpp")
text = path.read_text(encoding="utf-8")

old = "#include <SDL.h>\n#include <string>\n"
new = "#include <SDL.h>\n#include <string>\n#include <algorithm>\n"

if new in text:
    print("ttrial.cpp already has <algorithm>.")
elif old in text:
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("Added <algorithm> to ttrial.cpp.")
else:
    raise SystemExit("Could not locate ttrial.cpp include block")

# Keep scanline pitch tied to the original System 16 pixel rows without
# darkening an entire Nx engine-resolution pixel block. At 2x/3x/4x only one
# internal row per original source row is dimmed. This preserves the thin
# scanline look of the existing 2x renderer while higher engine resolutions
# only improve the image underneath it.
path = Path("src/main/sdl2/rendersurface.cpp")
text = path.read_text(encoding="utf-8")

old32 = """    const size_t render_scale = static_cast<size_t>(std::clamp(config.video.hires + 1, 1, 4));\n\n    for (size_t y = starty; y < endy; ++y) {\n        // Preserve the original 224-line scanline raster. At Nx engine\n        // resolution, all N internal rows belonging to the same native row\n        // receive the same scanline treatment.\n        if (((y / render_scale) & 1u) == 0)\n            continue;\n\n        uint32_t *row = pixels + y * width;\n"""
new32 = """    const size_t render_scale = static_cast<size_t>(std::clamp(config.video.hires + 1, 1, 4));\n    const size_t scanline_pitch = render_scale > 1 ? render_scale : 2;\n    const size_t scanline_phase = scanline_pitch - 1;\n\n    for (size_t y = starty; y < endy; ++y) {\n        // One thin dark row per original System 16 pixel row. Do not darken\n        // the complete Nx block: higher engine resolution must not make the\n        // visible scanlines thicker. Original 1x retains the legacy every-\n        // other-row behaviour.\n        if ((y % scanline_pitch) != scanline_phase)\n            continue;\n\n        uint32_t *row = pixels + y * width;\n"""

old16 = """    const size_t render_scale = static_cast<size_t>(std::clamp(config.video.hires + 1, 1, 4));\n\n    for (size_t y = starty; y < endy; ++y) {\n        // Preserve the original 224-line scanline raster. At Nx engine\n        // resolution, all N internal rows belonging to the same native row\n        // receive the same scanline treatment.\n        if (((y / render_scale) & 1u) == 0)\n            continue;\n\n        uint16_t *row = pixels + y * width;\n"""
new16 = """    const size_t render_scale = static_cast<size_t>(std::clamp(config.video.hires + 1, 1, 4));\n    const size_t scanline_pitch = render_scale > 1 ? render_scale : 2;\n    const size_t scanline_phase = scanline_pitch - 1;\n\n    for (size_t y = starty; y < endy; ++y) {\n        // One thin dark row per original System 16 pixel row. Do not darken\n        // the complete Nx block: higher engine resolution must not make the\n        // visible scanlines thicker. Original 1x retains the legacy every-\n        // other-row behaviour.\n        if ((y % scanline_pitch) != scanline_phase)\n            continue;\n\n        uint16_t *row = pixels + y * width;\n"""

changed = False
if new32 not in text:
    if old32 not in text:
        raise SystemExit("Could not locate current 32-bit scanline loop")
    text = text.replace(old32, new32, 1)
    changed = True

if new16 not in text:
    if old16 not in text:
        raise SystemExit("Could not locate current 16-bit scanline loop")
    text = text.replace(old16, new16, 1)
    changed = True

if changed:
    path.write_text(text, encoding="utf-8")
    print("Changed scanlines to one thin row per native System 16 pixel row.")
else:
    print("Scanlines already use thin native-row spacing.")
