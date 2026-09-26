from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def replace_once(path, old, new):
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")

replace_once(
    "src/main/sdl2/gl_backend.hpp",
    '''// Reallocate overlay texture storage to match current overlay pixel format.\n// Call if you change overlay format after init().\ninline void reallocate_overlay_storage() {\n    glActiveTexture(GL_TEXTURE1);\n    glBindTexture(GL_TEXTURE_2D, G.texOverlay);\n    GLenum ov_ifmt = (G.overlayFmt == State::PixFmt::A8) ? GL_LUMINANCE : GL_RGBA;\n    GLenum ov_fmt  = ov_ifmt;\n    glTexImage2D(GL_TEXTURE_2D, 0, ov_ifmt, G.overlayW, G.overlayH, 0, ov_fmt, GL_UNSIGNED_BYTE, nullptr);\n}\n''',
    '''// Reallocate overlay texture storage to match the current pixel format and,\n// when supplied, the current presentation geometry. Window/display resizes must\n// update these cached dimensions before uploading the rebuilt CRT overlay.\ninline void reallocate_overlay_storage(int width = 0, int height = 0) {\n    if (width > 0)  G.overlayW = width;\n    if (height > 0) G.overlayH = height;\n\n    glActiveTexture(GL_TEXTURE1);\n    glBindTexture(GL_TEXTURE_2D, G.texOverlay);\n    GLenum ov_ifmt = (G.overlayFmt == State::PixFmt::A8) ? GL_LUMINANCE : GL_RGBA;\n    GLenum ov_fmt  = ov_ifmt;\n    glTexImage2D(GL_TEXTURE_2D, 0, ov_ifmt, G.overlayW, G.overlayH, 0, ov_fmt, GL_UNSIGNED_BYTE, nullptr);\n}\n''')

replace_once(
    "src/main/sdl2/rendersurface.cpp",
    '''    glb::set_overlay_pixel_format_a8();\n    glb::reallocate_overlay_storage();\n\n    glb::update_overlay_texture( a8.data(),dst_rect.w,dst_rect.w,dst_rect.h );\n''',
    '''    glb::set_overlay_pixel_format_a8();\n    glb::reallocate_overlay_storage(dst_rect.w, dst_rect.h);\n\n    glb::update_overlay_texture( a8.data(),dst_rect.w,dst_rect.w,dst_rect.h );\n''')

print("Overlay resize follow-up applied")
