from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Expose a renderer hook through the existing abstract interface. Renderers that
# do not use the NTSC filter keep the no-op default.
replace_once(
    "src/main/sdl2/renderbase.hpp",
    "    virtual bool supports_vsync() { return false; }\n    virtual void focus_window() {}\n",
    "    virtual bool supports_vsync() { return false; }\n    virtual void focus_window() {}\n    virtual void advance_blargg_phase() {}\n",
)

replace_once(
    "src/main/sdl2/rendersurface.hpp",
    "    void focus_window() override;\n    bool start_frame() {return true;};\n",
    "    void focus_window() override;\n    void advance_blargg_phase() override;\n    bool start_frame() {return true;};\n",
)

# Advance phase only from the main thread at the frame boundary. This preserves
# DX's existing 30/60/120 cadence exactly while removing the render-worker race.
insert_after = '''void RenderSurface::focus_window()\n{\n    if (!window)\n        return;\n\n    // Do this only when CannonBall explicitly asks for focus after startup or\n    // a renderer restart. SDL window creation/fullscreen transitions alone do\n    // not reliably leave the new window focused on Windows.\n    SDL_ShowWindow(window);\n    SDL_RaiseWindow(window);\n\n    if (SDL_SetWindowInputFocus(window) != 0)\n    {\n        std::cerr << "Unable to focus CannonBall window: "\n                  << SDL_GetError() << std::endl;\n    }\n}\n'''
insert_new = insert_after + '''\nvoid RenderSurface::advance_blargg_phase()\n{\n    if (!blargg)\n        return;\n\n    // Keep the cadence used by DX before SE 1.50's threading fix. The only\n    // change is ownership: one main-thread update per rendered frame instead\n    // of a write from one of two concurrently running render workers.\n    if (config.fps == 60)\n        phase = (phase + 1) % 3;\n    else\n        phase = (phase + 2) % 3;\n}\n'''
replace_once("src/main/sdl2/rendersurface.cpp", insert_after, insert_new)

# The bottom half must begin at the burst phase a continuous full-frame blit
# would have reached after processing the top half's rows.
replace_once(
    "src/main/sdl2/rendersurface.cpp",
    "    const int block_height = section >= 0 ? (src_height >> 1) : src_height;\n    const int start_y = section == 1 ? (src_height >> 1) : 0;\n\n    if (!blargg)\n        return;\n",
    "    const int block_height = section >= 0 ? (src_height >> 1) : src_height;\n    const int start_y = section == 1 ? (src_height >> 1) : 0;\n    const int section_phase = section == 1\n        ? (phase + block_height) % 3\n        : phase;\n\n    if (!blargg)\n        return;\n",
)

# Replace the three blitter phase arguments in this function only. Limit the
# replacement to the blargg_filter body to avoid touching unrelated code.
p = Path("src/main/sdl2/rendersurface.cpp")
text = p.read_text(encoding="utf-8")
start = text.index("void RenderSurface::blargg_filter")
end = text.index("\n\n#include <stdint.h>", start)
body = text[start:end]
count = body.count("                phase,\n") + body.count("            phase,\n")
if count != 3:
    raise RuntimeError(f"blargg_filter: expected 3 phase arguments, found {count}")
body = body.replace("                phase,\n", "                section_phase,\n")
body = body.replace("            phase,\n", "            section_phase,\n")
text = text[:start] + body + text[end:]
p.write_text(text, encoding="utf-8")

# Remove phase mutation from worker-owned draw_frame().
replace_once(
    "src/main/sdl2/rendersurface.cpp",
    "        if (fastpass!=1) {\n            if (config.fps == 60) phase = (phase + 1) % 3; // cycle through 0/1/2\n            else                  phase = (phase + 2) % 3; // cycle through 0/1/2, but at twice the rate\n        }\n        blargg_filter(pixels, writePixels, fastpass);\n",
    "        blargg_filter(pixels, writePixels, fastpass);\n",
)

# Pass the safe-point hook through Video. DX keeps the preserved implementation
# in video_bugfix_base.cpp, included by the thin video.cpp wrapper.
replace_once(
    "src/main/video.hpp",
    "    void render_frame(int fastpass);\n    void present_frame();\n",
    "    void render_frame(int fastpass);\n    void present_frame();\n    void advance_blargg_phase();\n",
)

replace_once(
    "src/main/video_bugfix_base.cpp",
    "void Video::present_frame()\n{\n\trenderer->finalize_frame();\n}\n",
    "void Video::present_frame()\n{\n\trenderer->finalize_frame();\n}\n\nvoid Video::advance_blargg_phase()\n{\n    renderer->advance_blargg_phase();\n}\n",
)

# In threaded mode all previous render jobs are idle at the top of each normal
# iteration (their done semaphores were acquired before the previous swap). Set
# the frame's phase before releasing either render worker. In sequential mode do
# it immediately before render_frame().
replace_once(
    "src/main/main.cpp",
    "        if (using_threading) {\n            // Set NTSC filter to work on the last complete frame immediately\n            renderReady0.release();\n",
    "        if (using_threading) {\n            // Both render workers are idle here. Advance the NTSC burst phase\n            // once on the main thread before either half-frame worker can read it.\n            video.advance_blargg_phase();\n\n            // Set NTSC filter to work on the last complete frame immediately\n            renderReady0.release();\n",
)

replace_once(
    "src/main/main.cpp",
    "            video.prepare_frame();\n            video.render_frame(-1);\n            video.present_frame();\n",
    "            video.prepare_frame();\n            video.advance_blargg_phase();\n            video.render_frame(-1);\n            video.present_frame();\n",
)

print("SE 1.50 CRT two-thread seam fix adapted for DX successfully")
