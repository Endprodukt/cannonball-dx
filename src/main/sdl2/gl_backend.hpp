#pragma once
// Minimal OpenGL ES 2.0 backend for CannonBall-SE.
// Header-only. Uses SDL2 to create the context and <SDL_opengles2.h> for GL.
// Link with -lSDL2 -lGLESv2 on Linux/Raspberry Pi.
// Copyright (c) 2025, James Pearce.
//
// Overlay textures are applied as 8-bit (1 byte/pixel) alpha masks.
// 0xFF = transparent and 0x0 = fully black.

#include <SDL.h>
#include <SDL_opengles2.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace glb {

// ---------------- Default shaders (pass-through) ----------------
static const char* kDefaultVS =
    "precision mediump float;\n"
    "attribute vec2 VertexCoord;\n"
    "attribute vec2 TexCoord;\n"
    "varying vec2 vUV;\n"
    "void main(){\n"
    "    vUV = TexCoord;\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "}\n";

static const char* kDefaultFS =
    // 'Default' shader used when none provided.
    // It multiplies uTex0 by uTex1 when present.
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex0;\n"
    "uniform sampler2D uTex1;\n"
    "void main(){\n"
    "    gl_FragColor = texture2D(uTex0, vUV) * texture2D(uTex1, vUV);\n"
    "}\n";

// ---------------- Internal helpers ----------------
static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint n = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &n);
        std::string log; log.resize(n > 0 ? n : 1);
        glGetShaderInfoLog(s, n, nullptr, log.data());
        std::cerr << "GLSL compile failed: " << log << "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint makeProgram(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER,   vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    // Bind known attribute names for deterministic locations
    glBindAttribLocation(p, 0, "VertexCoord");
    glBindAttribLocation(p, 1, "TexCoord");
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint n = 0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &n);
        std::string log; log.resize(n > 0 ? n : 1);
        glGetProgramInfoLog(p, n, nullptr, log.data());
        std::cerr << "GLSL link failed: " << log << "\n";
        glDeleteProgram(p);
        p = 0;
    }
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    return p;
}

static void resolveAttribs(GLuint prog, GLint& locPos, GLint& locUV){
    locPos = glGetAttribLocation(prog, "VertexCoord");
    locUV  = glGetAttribLocation(prog, "TexCoord");
}

inline bool hasExtensionStr(const char* name) {
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!ext || !name) return false;
    return std::strstr(ext, name) != nullptr;
}

// Robust, low-overhead texture upload helper.
// Uploads RGBA/ABGR 8bpp with UNPACK_ALIGNMENT=4 and uses GL_EXT_unpack_subimage
// to respect row pitches when available. Otherwise packs once into G.scratch.
static void upload_rgba8(GLuint tex, GLenum fmt, const void* pixels, int pitchBytes, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const bool tight = (pitchBytes == w * 4);
    const bool haveRowLen = hasExtensionStr("GL_EXT_unpack_subimage");
    if (tight) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, pixels);
    } else if (haveRowLen) {
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, pitchBytes / 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#else
        // Fallback pack if the enum is missing in headers
        std::vector<uint8_t> tmp; tmp.resize((size_t)w * h * 4);
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&tmp[(size_t)y * w * 4], src + (size_t)y * pitchBytes, (size_t)w * 4);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, tmp.data());
#endif
    } else {
        // pack row-by-row into a tight buffer
        std::vector<uint8_t> tmp; tmp.resize((size_t)w * h * 4);
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&tmp[(size_t)y * w * 4], src + (size_t)y * pitchBytes, (size_t)w * 4);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, tmp.data());
    }
}

// Uploads 8-bit single-channel textures (LUMINANCE) with UNPACK_ALIGNMENT=1 and optional GL_EXT_unpack_subimage.
static void upload_alpha8(GLuint tex, GLenum fmt, const void* pixels, int pitchBytes, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool tight = (pitchBytes == w * 1);
    const bool haveRowLen = hasExtensionStr("GL_EXT_unpack_subimage");
    if (tight) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, pixels);
    } else if (haveRowLen) {
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, pitchBytes);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#else
        // Fallback pack if the enum is missing in headers
        std::vector<uint8_t> tmp; tmp.resize((size_t)w * h);
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&tmp[(size_t)y * w], src + (size_t)y * pitchBytes, (size_t)w);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, tmp.data());
#endif
    } else {
        // pack row-by-row into a tight buffer
        std::vector<uint8_t> tmp; tmp.resize((size_t)w * h);
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&tmp[(size_t)y * w], src + (size_t)y * pitchBytes, (size_t)w);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, tmp.data());
    }
}

// 16bpp uploader (RGB5_A1). Uses row length when available; otherwise packs rows tightly.
static void upload_rgb555_16(GLuint tex, const void* pixels, int pitchBytes, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    const bool tight = (pitchBytes == w * 2);
    const bool haveRowLen = hasExtensionStr("GL_EXT_unpack_subimage");
    if (tight) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, pixels);
    } else if (haveRowLen) {
    #ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, pitchBytes / 2);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    #else
        std::vector<uint16_t> tmp; tmp.resize((size_t)w * h);
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&tmp[(size_t)y * w], src + (size_t)y * pitchBytes, (size_t)w * 2);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, tmp.data());
    #endif
    } else {
        std::vector<uint16_t> tmp; tmp.resize((size_t)w * h);
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&tmp[(size_t)y * w], src + (size_t)y * pitchBytes, (size_t)w * 2);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, tmp.data());
    }
}



static GLuint makeVBO(const float* verts, size_t bytes){
    GLuint b = 0; glGenBuffers(1, &b);
    glBindBuffer(GL_ARRAY_BUFFER, b);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, verts, GL_STATIC_DRAW);
    return b;
}

// ---------------- State ----------------
struct State {
    SDL_Window* window = nullptr;

    // GL objects
    GLuint program = 0;          // main program (game shader)
    GLuint vbo = 0;              // fullscreen triangle VBO
    GLuint texGame = 0;          // game frame (sampler unit 0)
    GLuint texOverlay = 0;       // overlay (sampler unit 1)
    GLuint texWhite  = 0;        // 1x1 white (neutral overlay)
    bool   overlayReady = false;

    // Optional offscreen
    GLuint fbo = 0;
    GLuint texPass = 0;
    int fboW = 0, fboH = 0;

    // Backbuffer / logical sizes
    int fbW = 0, fbH = 0;
    int gameW = 0, gameH = 0;
    int overlayW = 0, overlayH = 0;

    // Destination rects
    bool useDstRect = false; int dstX=0,dstY=0,dstW=0,dstH=0;
    bool useOverlayDstRect = false; int ovDstX=0,ovDstY=0,ovDstW=0,ovDstH=0;

    // Simple uniform cache
    std::unordered_map<std::string, GLint> ucache;
    GLuint ucache_program = 0;

    // Resolved attribute locations
    GLint locPos = -1;        // main program
    GLint locUV  = -1;

    // Pixel format for uploads
    enum class PixFmt { RGBA, ABGR, A8, RGB555 };
    PixFmt gameFmt    = PixFmt::RGBA;
    PixFmt overlayFmt = PixFmt::A8; // default to compact 8-bit overlays

    // Upload helpers
    std::vector<uint8_t> scratch; // temp row buffer for CPU swizzles
    bool hasABGR = false;         // GL_EXT_abgr present

    // We target GLES2 everywhere
    bool isGLES = true;
};

//static State G; // internal linkage in each TU using this header
inline State G{};

// Fullscreen triangle vertices: x,y,u,v
static const float kFSVerts[12] = {
    -1.f, -1.f, 0.f, 1.f,
     3.f, -1.f, 2.f, 1.f,
    -1.f,  3.f, 0.f,-1.f
};

static GLuint makeProgram(const char* vs, const char* fs);

// ---------------- Default program creation ----------------
static void resolveAttribs(GLuint prog, GLint& locPos, GLint& locUV);

static GLuint makeVBO(const float* verts, size_t bytes);

static GLuint makeProgram(const char* vs, const char* fs);

static void resolveAttribs(GLuint prog, GLint& locPos, GLint& locUV);

static GLuint makeVBO(const float* verts, size_t bytes);

static GLuint makeProgram(const char* vs, const char* fs);

static void resolveAttribs(GLuint prog, GLint& locPos, GLint& locUV);

static GLuint makeVBO(const float* verts, size_t bytes);

static GLuint makeProgram(const char* vs, const char* fs);

static void resolveAttribs(GLuint prog, GLint& locPos, GLint& locUV);

static GLuint makeVBO(const float* verts, size_t bytes);

// ---------------- Default shaders (pass-through) ----------------

static GLuint makeProgram(const char* vs, const char* fs);

static void resolveAttribs(GLuint prog, GLint& locPos, GLint& locUV);

static GLuint makeVBO(const float* verts, size_t bytes);

static GLuint makeProgram(const char* vs, const char* fs);

static void resolveAttribs(GLuint prog, GLint& locPos, GLint& locUV);

static GLuint makeVBO(const float* verts, size_t bytes);

// ---------------- Default program creation ----------------
static void resolveAttribs(GLuint prog, GLint& locPos, GLint& locUV);

inline void loadShaders(const char* vertexSrc, const char* fragmentSrc)
{
    // -- D: defensive reset before creating the new program
    if (G.program) { glDeleteProgram(G.program); G.program = 0; }
    G.locPos = G.locUV = -1;
    G.ucache.clear();
    G.ucache_program = 0;

    // Compile/link the (possibly new) program
    G.program = makeProgram(vertexSrc ? vertexSrc : kDefaultVS,
                            fragmentSrc ? fragmentSrc : kDefaultFS);
    glUseProgram(G.program);

    // Bind common sampler names to unit 0
    if (GLint s0 = glGetUniformLocation(G.program, "uTex0");    s0 >= 0) glUniform1i(s0, 0);
    if (GLint t  = glGetUniformLocation(G.program, "Texture");  t  >= 0) glUniform1i(t,  0);

    // Bind optional overlay samplers to unit 1 (single-pass overlay)
    if (GLint s1 = glGetUniformLocation(G.program, "uTex1");    s1 >= 0) glUniform1i(s1, 1);
    if (GLint o  = glGetUniformLocation(G.program, "Overlay");  o  >= 0) glUniform1i(o,  1);

    // Only discover locations here; set pointers later in draw(), guarded by >= 0
    resolveAttribs(G.program, G.locPos, G.locUV);
}

// Forward-declare uniform cache helper
inline GLint uget(const char* name);

// ---------------- Public API ----------------
inline bool init(SDL_Window* window,
                 int gameW, int gameH,
                 int overlayW, int overlayH,
                 const char* vertexSrc = kDefaultVS,
                 const char* fragmentSrc = kDefaultFS,
                 bool createOffscreen = false,
                 int offscreenW = 0, int offscreenH = 0)
{
    G.window   = window;
    G.gameW    = gameW;
    G.overlayW = overlayW;
    G.gameH    = gameH;
    G.overlayH = overlayH;

    SDL_GL_GetDrawableSize(G.window, &G.fbW, &G.fbH);
    glViewport(0, 0, G.fbW, G.fbH);

    // Assume GLES on all our targets
    G.isGLES = true;

    // Create textures up-front
    glGenTextures(1, &G.texGame);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, G.texGame);

    GLenum game_ifmt = (G.gameFmt == State::PixFmt::RGB555) ? GL_RGB5_A1 : GL_RGBA;
    GLenum game_fmt  = (G.gameFmt == State::PixFmt::RGB555) ? GL_RGBA    : GL_RGBA;
    GLenum game_type = (G.gameFmt == State::PixFmt::RGB555) ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_BYTE;

    if (vertexSrc && fragmentSrc) {
        // use GL_Linear as user shader will likely curve etc
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        // use cheaper GL_NEAREST which will be fine for texture expansion
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, game_ifmt, gameW, gameH, 0, game_fmt, game_type, nullptr);

    // Overlay texture
    glGenTextures(1, &G.texOverlay);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, G.texOverlay);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Use 8-bit single-channel for overlays by default (GL_LUMINANCE replicates to RGB in GLES2)
    GLenum ov_ifmt = (G.overlayFmt == State::PixFmt::A8) ? GL_LUMINANCE : GL_RGBA;
    GLenum ov_fmt  = ov_ifmt;
    glTexImage2D(GL_TEXTURE_2D, 0, ov_ifmt, overlayW, overlayH, 0, ov_fmt, GL_UNSIGNED_BYTE, nullptr);

    // 1x1 texture for use when overlay not in use
    glGenTextures(1, &G.texWhite);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, G.texWhite);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLubyte kWhitePixel[4] = {255,255,255,255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, kWhitePixel);

    // Optional off-screen
    if (createOffscreen) {
        G.fboW = offscreenW > 0 ? offscreenW : G.fbW;
        G.fboH = offscreenH > 0 ? offscreenH : G.fbH;
        glGenTextures(1, &G.texPass);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, G.texPass);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, G.fboW, G.fboH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &G.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, G.texPass, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Main program (game shader)
    loadShaders(vertexSrc, fragmentSrc);

    // Create fullscreen triangle VBO if not already created
    if (!G.vbo) {
        G.vbo = makeVBO(kFSVerts, sizeof(kFSVerts));
    }

    // Discover ABGR extension once
#ifndef GL_ABGR_EXT
#define GL_ABGR_EXT 0x8000
#endif
    G.hasABGR = hasExtensionStr("GL_EXT_abgr");

    return G.program != 0;
}

// -------- Pixel format setters --------
inline void set_game_pixel_format_abgr(bool isABGR){
    G.gameFmt = isABGR ? State::PixFmt::ABGR : State::PixFmt::RGBA;
}
inline void set_overlay_pixel_format_abgr(bool isABGR){
    G.overlayFmt = isABGR ? State::PixFmt::ABGR : State::PixFmt::RGBA;
}
inline void set_game_pixel_format(State::PixFmt fmt){ G.gameFmt = fmt; }
inline void set_overlay_pixel_format(State::PixFmt fmt){ G.overlayFmt = fmt; }

// Convenience helpers for 8-bit overlays
inline void set_overlay_pixel_format_alpha8(bool enableA8){
    G.overlayFmt = enableA8 ? State::PixFmt::A8 : State::PixFmt::RGBA;
}
inline void set_overlay_pixel_format_a8(){ G.overlayFmt = State::PixFmt::A8; }

// -------- Auto-detect from SDL_Surface --------
inline State::PixFmt deduce_pixfmt_from_surface(const SDL_Surface* s){
    if (!s || !s->format) return State::PixFmt::RGBA;
    const SDL_PixelFormat* f = s->format;
    if (f->BitsPerPixel == 8) {
        return State::PixFmt::A8;
    }
    if (f->BitsPerPixel == 16) {
        return State::PixFmt::RGB555;
    }
    if (f->BitsPerPixel != 32) {
        // Fallback to RGBA when unknown
        return State::PixFmt::RGBA;
    }
    auto idx = [](Uint32 m)->int{
        if (m == 0x000000FFu) return 0; // byte 0
        if (m == 0x0000FF00u) return 1; // byte 1
        if (m == 0x00FF0000u) return 2; // byte 2
        if (m == 0xFF000000u) return 3; // byte 3
        return -1; // unknown
    };
    const int ri = idx(f->Rmask);
    const int gi = idx(f->Gmask);
    const int bi = idx(f->Bmask);
    const int ai = idx(f->Amask);
    // Quick heuristic: if bytes look like ABGR order, report ABGR
    if (ri==3 && gi==2 && bi==1 && ai==0) return State::PixFmt::ABGR;
    return State::PixFmt::RGBA;
}

inline void auto_configure_pixel_formats_from_surfaces(const SDL_Surface* game, const SDL_Surface* overlay){
    if (game)    set_game_pixel_format(deduce_pixfmt_from_surface(game));
    if (overlay) set_overlay_pixel_format(deduce_pixfmt_from_surface(overlay));
}

// Back-compat alias (shorter name)
inline void auto_configure_pixel_formats(const SDL_Surface* game, const SDL_Surface* overlay){
    auto_configure_pixel_formats_from_surfaces(game, overlay);
}

// Reallocate overlay texture storage to match current overlay pixel format.
// Call if you change overlay format after init().
inline void reallocate_overlay_storage() {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, G.texOverlay);
    GLenum ov_ifmt = (G.overlayFmt == State::PixFmt::A8) ? GL_LUMINANCE : GL_RGBA;
    GLenum ov_fmt  = ov_ifmt;
    glTexImage2D(GL_TEXTURE_2D, 0, ov_ifmt, G.overlayW, G.overlayH, 0, ov_fmt, GL_UNSIGNED_BYTE, nullptr);
}

// -------- Uploads --------
inline void update_game_texture(const void* pixels, int pitchBytes, int w, int h) {
    glActiveTexture(GL_TEXTURE0);

     if (G.gameFmt == State::PixFmt::RGB555) {
        upload_rgb555_16(G.texGame, pixels, pitchBytes, w, h);
        return;
    }

    GLenum fmt = GL_RGBA;
    bool needConvert = false;
    if (G.gameFmt == State::PixFmt::ABGR) {
        if (G.hasABGR) {
            fmt = GL_ABGR_EXT;
        } else {
            if (hasExtensionStr("GL_EXT_texture_swizzle")) {
                glBindTexture(GL_TEXTURE_2D, G.texGame);
#ifdef GL_TEXTURE_SWIZZLE_R
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ALPHA);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_BLUE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_GREEN);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
#else
                needConvert = true;
#endif
            } else {
                needConvert = true;
            }
        }
    }
    if (!needConvert) {
        upload_rgba8(G.texGame, fmt, pixels, pitchBytes, w, h);
    } else {
        G.scratch.resize((size_t)w * h * 4);
        const uint8_t* srow = static_cast<const uint8_t*>(pixels);
        uint8_t* d = G.scratch.data();
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = srow + (size_t)y * pitchBytes;
            for (int x = 0; x < w; ++x) {
                uint8_t a = row[0], b = row[1], g = row[2], r = row[3];
                *d++ = r; *d++ = g; *d++ = b; *d++ = a;
                row += 4;
            }
        }
        upload_rgba8(G.texGame, GL_RGBA, G.scratch.data(), w*4, w, h);
    }
}


inline void update_overlay_texture(const void* pixels, int pitchBytes, int w, int h) {
    glActiveTexture(GL_TEXTURE1);
    if (G.overlayFmt == State::PixFmt::A8) {
        // Alpha-only overlays are uploaded as GL_LUMINANCE (replicated to RGB, A=1) for correct multiply in GLES2
        upload_alpha8(G.texOverlay, GL_LUMINANCE, pixels, pitchBytes, w, h);
        G.overlayReady = true;
        return;
    }
    GLenum fmt = GL_RGBA;
    bool needConvert = false;
    if (G.overlayFmt == State::PixFmt::ABGR) {
        if (G.hasABGR) {
            fmt = GL_ABGR_EXT;
        } else {
            if (hasExtensionStr("GL_EXT_texture_swizzle")) {
                glBindTexture(GL_TEXTURE_2D, G.texOverlay);
#ifdef GL_TEXTURE_SWIZZLE_R
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ALPHA);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_BLUE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_GREEN);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
#else
                needConvert = true;
#endif
            } else {
                needConvert = true;
            }
        }
    }
    if (!needConvert) {
        upload_rgba8(G.texOverlay, fmt, pixels, pitchBytes, w, h);
    } else {
        G.scratch.resize((size_t)w * h * 4);
        const uint8_t* srow = static_cast<const uint8_t*>(pixels);
        uint8_t* d = G.scratch.data();
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = srow + (size_t)y * pitchBytes;
            for (int x = 0; x < w; ++x) {
                uint8_t a = row[0], b = row[1], g = row[2], r = row[3];
                *d++ = r; *d++ = g; *d++ = b; *d++ = a;
                row += 4;
            }
        }
        upload_rgba8(G.texOverlay, GL_RGBA, G.scratch.data(), w*4, w, h);
    }
    G.overlayReady = true;
}


// Clear the currently uploaded overlay and revert to the neutral 1x1 white texture.
// After calling this, draws will multiply against white (no overlay effect).
inline void clear_overlay_texture(){
    G.overlayReady = false;
    // Bind the neutral 1x1 white texture on unit 1 for immediate correctness.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, G.texWhite);
    glActiveTexture(GL_TEXTURE0);
}


// Uniform helpers (main program)
inline GLint uget(const char* name){
    if (G.ucache_program != G.program) { G.ucache.clear(); G.ucache_program = G.program; }
    auto it = G.ucache.find(name);
    if (it != G.ucache.end()) return it->second;
    GLint l = glGetUniformLocation(G.program, name);
    G.ucache[name] = l;
    return l;
}
inline void set_uniform(const char* name, float v)              { glUseProgram(G.program); GLint l=uget(name); if (l>=0) glUniform1f(l, v); }
inline void set_uniform2(const char* name, float x,float y)     { glUseProgram(G.program); GLint l=uget(name); if (l>=0) glUniform2f(l, x,y); }
inline void set_uniform3(const char* name, float x,float y,float z){ glUseProgram(G.program); GLint l=uget(name); if (l>=0) glUniform3f(l, x,y,z); }
inline void set_uniform4(const char* name, float x,float y,float z,float w){ glUseProgram(G.program); GLint l=uget(name); if (l>=0) glUniform4f(l, x,y,z,w); }

// -------- Destination rect controls (bottom-left origin) --------
inline void set_present_rect_pixels(int x, int y, int w, int h){
    G.dstX = x; G.dstY = y; G.dstW = w; G.dstH = h; G.useDstRect = true;
}
inline void clear_present_rect(){ G.useDstRect = false; }

inline void set_overlay_rect_pixels(int x, int y, int w, int h){
    G.ovDstX = x; G.ovDstY = y; G.ovDstW = w; G.ovDstH = h; G.useOverlayDstRect = true;
}
inline void clear_overlay_rect(){ G.useOverlayDstRect = false; }

inline void draw(bool useOffscreen, bool drawOverlay) {
    // --- Pass A: draw game texture ---
    if (useOffscreen && G.fbo) {
        // 1) game -> offscreen
        glDisable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ZERO);
        glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);
        glViewport(0, 0, G.fboW, G.fboH);
        glUseProgram(G.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, G.texGame);
        // Neutralize overlay during offscreen subpass
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, G.texWhite);
        glActiveTexture(GL_TEXTURE0);
        glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
        if (G.locPos >= 0) {
            glEnableVertexAttribArray(G.locPos);
            glVertexAttribPointer(G.locPos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)0);
        }
        if (G.locUV >= 0) {
            glEnableVertexAttribArray(G.locUV);
            glVertexAttribPointer(G.locUV,  2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)(2*sizeof(float)));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 2) offscreen -> backbuffer, with optional overlay multiply
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int vx = 0, vy = 0, vw = G.fbW, vh = G.fbH;
        if (G.useDstRect) {
            vx = G.dstX; vw = G.dstW; vh = G.dstH; vy = 0 + G.dstY; // bottom-left origin
        }
        glViewport(vx, vy, vw, vh);
        glUseProgram(G.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, G.texPass);
        // Bind overlay (or white) for single-pass multiply
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, (drawOverlay && G.overlayReady) ? G.texOverlay : G.texWhite);
        glActiveTexture(GL_TEXTURE0);
        glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
        if (G.locPos >= 0) {
            glEnableVertexAttribArray(G.locPos);
            glVertexAttribPointer(G.locPos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)0);
        }
        if (G.locUV >= 0) {
            glEnableVertexAttribArray(G.locUV);
            glVertexAttribPointer(G.locUV,  2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)(2*sizeof(float)));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
    } else {
        // Single pass directly to backbuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int vx = 0, vy = 0, vw = G.fbW, vh = G.fbH;
        if (G.useDstRect) {
            vx = G.dstX; vw = G.dstW; vh = G.dstH; vy = 0 + G.dstY; // bottom-left origin
        }
        glViewport(vx, vy, vw, vh);
        glUseProgram(G.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, G.texGame);
        // Bind overlay (or white) for single-pass multiply
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, (drawOverlay && G.overlayReady) ? G.texOverlay : G.texWhite);
        glActiveTexture(GL_TEXTURE0);
        glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
        if (G.locPos >= 0) {
            glEnableVertexAttribArray(G.locPos);
            glVertexAttribPointer(G.locPos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)0);
        }
        if (G.locUV >= 0) {
            glEnableVertexAttribArray(G.locUV);
            glVertexAttribPointer(G.locUV,  2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)(2*sizeof(float)));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}

// Optional helper to clear the frame
inline void clear(float r, float g, float b, float a){
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

inline void shutdown() {
    if (G.vbo)        { glDeleteBuffers(1, &G.vbo); G.vbo = 0; }
    if (G.texWhite)   { glDeleteTextures(1, &G.texWhite);   G.texWhite = 0; }
    if (G.texGame)    { glDeleteTextures(1, &G.texGame); G.texGame = 0; }
    if (G.texOverlay) { glDeleteTextures(1, &G.texOverlay); G.texOverlay = 0; }
    if (G.texPass)    { glDeleteTextures(1, &G.texPass); G.texPass = 0; }
    if (G.fbo)       { glDeleteFramebuffers(1, &G.fbo); G.fbo = 0; }
    if (G.program)    { glDeleteProgram(G.program); G.program = 0; }

    // The SDL window/context are destroyed by RenderSurface immediately after
    // this call. No window pointer, GL name or cached state may outlive them.
    G = State{};
}


// ---------------- Compatibility helpers ----------------
// Call this when the drawable/backbuffer size has changed.
inline void on_drawable_resized(){
    if (!G.window) return;
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(G.window, &w, &h);
    if (w <= 0 || h <= 0) return;
    if (w == G.fbW && h == G.fbH) return;
    G.fbW = w; G.fbH = h;

    // Respect any active present rect; otherwise use full backbuffer
    int vx = 0, vy = 0, vw = w, vh = h;
    if (G.useDstRect) { vx = G.dstX; vy = G.dstY; vw = G.dstW; vh = G.dstH; }
    glViewport(vx, vy, vw, vh);

    // If we have an offscreen pass, resize it to match the new drawable
    if (G.fbo && G.texPass) {
        glBindTexture(GL_TEXTURE_2D, G.texPass);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        G.fboW = w; G.fboH = h;
    }
}

// VSync / swap interval control: 0 = immediate, 1 = vsync, -1 = adaptive (if supported)
inline void set_swap_interval(int interval){
    if (SDL_GL_SetSwapInterval(interval) != 0) {
        std::cerr << "SDL_GL_SetSwapInterval failed: " << SDL_GetError() << "\n";
    }
}

// Present the backbuffer (swap)
inline void present(){
    if (G.window) SDL_GL_SwapWindow(G.window);
}

// Convenience: set present rectangle using top-left origin coordinates
inline void set_present_rect_pixels_top_left(int x, int y, int w, int h){
    // Convert to bottom-left origin expected by set_present_rect_pixels
    int fbw = G.fbW, fbh = G.fbH;
    if (G.window) {
        int cw=0,ch=0; SDL_GL_GetDrawableSize(G.window, &cw, &ch);
        if (cw>0 && ch>0) { fbw=cw; fbh=ch; G.fbW=cw; G.fbH=ch; }
    }
    int yBL = fbh - (y + h);
    set_present_rect_pixels(x, yBL, w, h);
}

// Convenience: set overlay rectangle using top-left origin coordinates
inline void set_overlay_rect_pixels_top_left(int x, int y, int w, int h){
    int fbw = G.fbW, fbh = G.fbH;
    if (G.window) {
        int cw=0,ch=0; SDL_GL_GetDrawableSize(G.window, &cw, &ch);
        if (cw>0 && ch>0) { fbw=cw; fbh=ch; G.fbW=cw; G.fbH=ch; }
    }
    int yBL = fbh - (y + h);
    set_overlay_rect_pixels(x, yBL, w, h);
}

} // namespace glb
