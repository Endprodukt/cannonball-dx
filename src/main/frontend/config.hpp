/***************************************************************************
    XML Configuration File Handling - CannonBall DX extensions.

    The current configuration declaration is preserved in config_base.hpp.
    This wrapper adds the persistent five-step bumper-view height setting
    without duplicating the inherited configuration structure.
***************************************************************************/

#pragma once

// Pre-include config_base.hpp dependencies before temporarily extending the
// class at its private section. This keeps the macro away from library headers.
#include <SDL.h>
#include <set>
#include <string>
#include <vector>
#include "stdint.hpp"
#include "highscore_storage.hpp"

#define CANNONBALL_DX_CONFIG_EXTENSIONS \
    static const int BUMPER_VIEW_HEIGHT_LEVELS = 5; \
    int bumper_view_height_level() \
    { \
        int level = cfg.get_int("engine.bumper_view_height", 1); \
        if (level < 0 || level >= BUMPER_VIEW_HEIGHT_LEVELS) \
            level = 1; \
        return level; \
    } \
    void set_bumper_view_height_level(int level) \
    { \
        if (level < 0) \
            level = 0; \
        else if (level >= BUMPER_VIEW_HEIGHT_LEVELS) \
            level = BUMPER_VIEW_HEIGHT_LEVELS - 1; \
        cfg.put_int("engine.bumper_view_height", level); \
    } \
    void cycle_bumper_view_height() \
    { \
        set_bumper_view_height_level( \
            (bumper_view_height_level() + 1) % BUMPER_VIEW_HEIGHT_LEVELS); \
    }

#define private public: CANNONBALL_DX_CONFIG_EXTENSIONS private
#include "config_base.hpp"
#undef private
#undef CANNONBALL_DX_CONFIG_EXTENSIONS
