/***************************************************************************
    Road Rendering & Control - CannonBall DX extensions.

    The inherited road implementation is preserved in oroad_base.cpp. This
    wrapper adds a persistent, bounded five-step Bumper View height control.
***************************************************************************/

#include <SDL.h>
#include <iostream>
#include <string>

#include "stdint.hpp"
#include "engine/oroad.hpp"

// Pre-include every dependency used by the preserved implementation before the
// temporary tick macro below so it can affect only ORoad::tick's definition.
#include "globals.hpp"
#include "roms.hpp"
#include "trackloader.hpp"
#include "frontend/config.hpp"
#include "engine/oaddresses.hpp"
#include "engine/outils.hpp"
#include "engine/oinitengine.hpp"
#include "engine/ostats.hpp"
#include "engine/ohud.hpp"

#define tick tick_base
#include "oroad_base.cpp"
#undef tick

namespace
{
    struct BumperHeightPreset
    {
        int16_t extra_horizon_offset;
        const char* name;
    };

    // The inherited Bumper View target remains -0x170. These bounded deltas
    // yield effective targets of -0x180, -0x170, -0x140, -0x100 and -0x0C0.
    // Existing users therefore retain today's camera position by default.
    const BumperHeightPreset BUMPER_HEIGHT_PRESETS[] =
    {
        { -0x10, "LOW" },
        {  0x00, "DEFAULT" },
        {  0x30, "MEDIUM" },
        {  0x70, "HIGH" },
        {  0xB0, "HIGHEST" },
    };

    bool f4_was_down = false;
    int16_t bumper_height_offset = 0;
    int bumper_height_message_ticks = 0;
    bool bumper_height_message_visible = false;

    int bumper_height_level()
    {
        int level = config.bumper_view_height_level();
        if (level < 0 || level >= Config::BUMPER_VIEW_HEIGHT_LEVELS)
            level = 1;
        return level;
    }

    bool bumper_height_hotkey_pressed()
    {
        const Uint8* keyboard_state = SDL_GetKeyboardState(nullptr);
        const bool down = keyboard_state &&
            keyboard_state[SDL_SCANCODE_F4] != 0;
        const bool pressed = down && !f4_was_down;
        f4_was_down = down;
        return pressed;
    }

    void approach_bumper_height(int16_t target)
    {
        const int16_t step = 0x10;

        if (bumper_height_offset < target)
        {
            bumper_height_offset += step;
            if (bumper_height_offset > target)
                bumper_height_offset = target;
        }
        else if (bumper_height_offset > target)
        {
            bumper_height_offset -= step;
            if (bumper_height_offset < target)
                bumper_height_offset = target;
        }
    }

    std::string bumper_height_message(int level)
    {
        return std::string("BUMPER HEIGHT ") +
            std::to_string(level + 1) + "/" +
            std::to_string(Config::BUMPER_VIEW_HEIGHT_LEVELS) + " " +
            BUMPER_HEIGHT_PRESETS[level].name;
    }

    void draw_bumper_height_message()
    {
        if (bumper_height_message_ticks > 0)
        {
            const int level = bumper_height_level();
            const std::string message = bumper_height_message(level);

            // Clear the full notification area first because preset names have
            // different lengths when cycling quickly.
            ohud.blit_text_new(
                6,
                24,
                "                            ",
                OHud::GREEN);
            ohud.blit_text_new(6, 24, message.c_str(), OHud::GREEN);

            bumper_height_message_ticks--;
            bumper_height_message_visible = true;
            return;
        }

        if (bumper_height_message_visible)
        {
            ohud.blit_text_new(
                6,
                24,
                "                            ",
                OHud::GREEN);
            bumper_height_message_visible = false;
        }
    }
}

void ORoad::tick()
{
    const bool bumper_view = get_view_mode() == VIEW_INCAR;
    const bool f4_pressed = bumper_height_hotkey_pressed();

    if (bumper_view && f4_pressed)
    {
        config.cycle_bumper_view_height();
        const int level = bumper_height_level();

        // Keep the setting persistent just like the other DX hotkey toggles.
        if (!config.save())
            std::cerr << "Unable to save bumper view height setting." << std::endl;

        bumper_height_message_ticks = config.tick_fps > 0
            ? config.tick_fps * 2
            : 60;

        std::cout
            << "Bumper View Height: "
            << (level + 1) << "/" << Config::BUMPER_VIEW_HEIGHT_LEVELS
            << " (" << BUMPER_HEIGHT_PRESETS[level].name << ")"
            << std::endl;
    }

    // Leaving Bumper View resets only the transient render delta. The selected
    // preset itself remains saved and is restored the next time Bumper View is
    // entered. Within Bumper View, changes ease in at the same 0x10 cadence as
    // the inherited camera transition rather than jumping abruptly.
    if (!bumper_view)
    {
        bumper_height_offset = 0;
    }
    else
    {
        approach_bumper_height(
            BUMPER_HEIGHT_PRESETS[bumper_height_level()].extra_horizon_offset);
    }

    // horizon_base is public engine state. Temporarily bias it for the road
    // calculation, then restore it immediately so stage/hill logic keeps its
    // canonical value and only the rendered Bumper View is affected.
    const int16_t applied_offset = bumper_view ? bumper_height_offset : 0;
    horizon_base += applied_offset;
    tick_base();
    horizon_base -= applied_offset;

    if (bumper_view)
        draw_bumper_height_message();
    else if (bumper_height_message_visible)
    {
        ohud.blit_text_new(
            6,
            24,
            "                            ",
            OHud::GREEN);
        bumper_height_message_visible = false;
        bumper_height_message_ticks = 0;
    }
}
