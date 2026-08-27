#pragma once

#include "globals.hpp"
#include "sdl2/audio.hpp"
#include "frontend/menulabels.hpp"

namespace cannonball
{
    extern Audio audio;

    // Frame counter
	extern int frame;

    // Tick Logic. Used when running at non-standard > 30 fps
    extern bool tick_frame;

    // Millisecond Time Per Frame
    extern double frame_ms;

    // FPS Counter
    extern int fps_counter;

    // Engine Master State
    extern int state;

    enum
    {
        STATE_BOOT,
        STATE_INIT_MENU,
        STATE_MENU,
        STATE_INIT_GAME,
        STATE_GAME,
        STATE_QUIT
    };

    // JJP
    extern int  fps_lock; // 0=use config setting, 30/60=command-line override
    extern bool singlecore_detect;
    extern bool singlecore_mode;
    extern int  game_threads;
    extern bool perftest;
}
