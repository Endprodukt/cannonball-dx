/***************************************************************************
    Bug-fix option wrapper for the current DX music-select implementation.

    The complete DX implementation is kept in omusic_bugfix_base.cpp. Its
    preserved legacy fix_bugs read is redirected to the dedicated misplaced
    Music Select tile option.
***************************************************************************/

#include <SDL.h>
#include <cstring>
#include <iostream>
#include <random>
#include "main.hpp"
#include "frontend/config.hpp"
#include "engine/car_palette_state.hpp"
#include "engine/oferrari.hpp"
#include "engine/ohiscore.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/ologo.hpp"
#include "engine/omusic.hpp"
#include "engine/otiles.hpp"
#include "engine/otraffic.hpp"
#include "engine/ostats.hpp"
#include "frontend/menu.hpp"
#include "directx/ffeedback.hpp"

#define fix_bugs fix_bugs && false || config.bugfix_music_select_tile()
#include "omusic_bugfix_base.cpp"
#undef fix_bugs
