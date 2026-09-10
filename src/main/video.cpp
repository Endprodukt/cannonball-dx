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
#include "video.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/oroad.hpp"
#include "engine/music_side_art.hpp"
#include "sdl2/pixelscaler_renderer.hpp"

#define fix_bugs fix_bugs && false || !config.bugfix_menu_map_road_line()
#include "video_bugfix_base.cpp"
#undef fix_bugs
