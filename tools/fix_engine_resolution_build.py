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

# Scanlines are a native System 16 raster effect. Higher Engine Resolution
# increases the number of internal rows that represent one original 224-line
# pixel row, so the scanline mask must follow the native row rather than every
# second internal row.
path = Path("src/main/sdl2/rendersurface.cpp")
text = path.read_text(encoding="utf-8")

old32 = """    for (size_t y = (starty+1); y < endy; y += 2) {\n        uint32_t *row = pixels + y * width;\n"""
new32 = """    const size_t render_scale = static_cast<size_t>(std::clamp(config.video.hires + 1, 1, 4));\n\n    for (size_t y = starty; y < endy; ++y) {\n        // Preserve the original 224-line scanline raster. At Nx engine\n        // resolution, all N internal rows belonging to the same native row\n        // receive the same scanline treatment.\n        if (((y / render_scale) & 1u) == 0)\n            continue;\n\n        uint32_t *row = pixels + y * width;\n"""

old16 = """    for (size_t y = starty + 1; y < endy; y += 2) {\n        uint16_t *row = pixels + y * width;\n"""
new16 = """    const size_t render_scale = static_cast<size_t>(std::clamp(config.video.hires + 1, 1, 4));\n\n    for (size_t y = starty; y < endy; ++y) {\n        // Preserve the original 224-line scanline raster. At Nx engine\n        // resolution, all N internal rows belonging to the same native row\n        // receive the same scanline treatment.\n        if (((y / render_scale) & 1u) == 0)\n            continue;\n\n        uint16_t *row = pixels + y * width;\n"""

changed = False
if new32 not in text:
    if old32 not in text:
        raise SystemExit("Could not locate 32-bit scanline loop")
    text = text.replace(old32, new32, 1)
    changed = True

if new16 not in text:
    if old16 not in text:
        raise SystemExit("Could not locate 16-bit scanline loop")
    text = text.replace(old16, new16, 1)
    changed = True

if changed:
    path.write_text(text, encoding="utf-8")
    print("Aligned scanlines to native System 16 pixel rows.")
else:
    print("Scanlines already use native System 16 row spacing.")
