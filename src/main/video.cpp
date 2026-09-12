/***************************************************************************
    Bug-fix option wrapper for the preserved video implementation.

    The complete DX video source is kept in video_bugfix_base.cpp. Its single
    negative fix_bugs test is redirected to the dedicated menu/map road-line
    option without changing the surrounding renderer code.
***************************************************************************/

#include <new>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <bit>
#include <algorithm>

#ifdef _WIN32
#include <cstdlib>

namespace
{
    // VRR test path: keep ANGLE/GLES2, but use ANGLE's desktop OpenGL backend
    // instead of its default D3D11 backend. ANGLE reads this environment
    // variable when EGL is initialized, which happens after program startup.
    // This lets the native OpenGL driver own presentation/swap control without
    // changing CannonBall's renderer or GLES2 shaders.
    struct AngleBackendSelector
    {
        AngleBackendSelector()
        {
            _putenv_s("ANGLE_DEFAULT_PLATFORM", "gl");
        }
    } angle_backend_selector;
}
#endif

#include "video.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/oroad.hpp"
#include "engine/music_side_art.hpp"
#include "sdl2/pixelscaler_renderer.hpp"

#define fix_bugs fix_bugs && false || !config.bugfix_menu_map_road_line()
#include "video_bugfix_base.cpp"
#undef fix_bugs
