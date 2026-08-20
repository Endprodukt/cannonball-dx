/***************************************************************************
    XML Configuration File Handling - CannonBall-SE extensions.

    The existing configuration implementation is retained in config_base.cpp.
    This wrapper adds persistent bindings for the three optional direct-view
    buttons while keeping all existing settings and SmartyPi behaviour intact.
***************************************************************************/

// Pre-include the base file's dependencies before the temporary method-name
// macros below. This keeps the macros away from standard-library headers.
#include <iostream>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <regex>
#include <map>
#include <algorithm>
#include <cctype>
#include <string>
#include <cstdio>

#include "main.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "../utils.hpp"
#include "engine/ohiscore.hpp"
#include "engine/outils.hpp"
#include "engine/audio/osoundint.hpp"

// Retain the existing Config::load/save implementation under private names.
#define load load_base
#define save save_base
#include "config_base.cpp"
#undef load
#undef save

void Config::load()
{
    load_base();

    // Optional direct camera selection bindings. -1 means unassigned.
    controls.keyconfig[12] = cfg.get_int("controls.keyconfig.view1", -1);
    controls.keyconfig[13] = cfg.get_int("controls.keyconfig.view2", -1);
    controls.keyconfig[14] = cfg.get_int("controls.keyconfig.view3", -1);

    // Slots 12-14 remain the original cabinet motor-limit inputs.
    // The three new view buttons therefore use slots 15-17.
    controls.padconfig[15] = cfg.get_int("controls.padconfig.view1", -1);
    controls.padconfig[16] = cfg.get_int("controls.padconfig.view2", -1);
    controls.padconfig[17] = cfg.get_int("controls.padconfig.view3", -1);
}

bool Config::save()
{
    // Add the direct-view bindings to the same config tree before the existing
    // save routine writes it. Unknown/existing settings remain untouched.
    cfg.put_int("controls.keyconfig.view1", controls.keyconfig[12]);
    cfg.put_int("controls.keyconfig.view2", controls.keyconfig[13]);
    cfg.put_int("controls.keyconfig.view3", controls.keyconfig[14]);

    cfg.put_int("controls.padconfig.view1", controls.padconfig[15]);
    cfg.put_int("controls.padconfig.view2", controls.padconfig[16]);
    cfg.put_int("controls.padconfig.view3", controls.padconfig[17]);

    return save_base();
}
