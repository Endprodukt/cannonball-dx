/***************************************************************************
    Front End Menu System - CannonBall-SE external-output extensions.

    The existing menu implementation is retained in menu_base.cpp. Menu derives
    from MenuBase so the unified control-binding wizard can append the three
    optional direct-view buttons without changing the rest of the menu code.
***************************************************************************/

// Pre-include dependencies before the temporary class-name macro.
#include "main.hpp"
#include "menu.hpp"
#include "menulabels.hpp"
#include "../utils.hpp"

#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/osprites.hpp"
#include "engine/ologo.hpp"
#include "engine/omusic.hpp"
#include "engine/opalette.hpp"
#include "engine/otiles.hpp"

#include "frontend/cabdiag.hpp"
#include "frontend/ttrial.hpp"

#include <iostream>
#include "directx/ffeedback.hpp"
#include <string>
#include <string_view>
#include <cctype>
#include <algorithm>

// Compile the existing Menu implementation as MenuBase. Calls to the virtual
// redefine_joystick() from MenuBase::tick_ui() dispatch to Menu below.
#define Menu MenuBase
#include "menu_base.cpp"
#undef Menu

void Menu::redefine_joystick()
{
    // Run the existing unified configuration wizard unchanged through VIEW.
    // It leaves redef_state at 13 after the VIEW control has been released.
    if (redef_state < 13)
    {
        MenuBase::redefine_joystick();
        return;
    }

    enum DirectWaitType
    {
        DIRECT_WAIT_NONE,
        DIRECT_WAIT_KEYBOARD,
        DIRECT_WAIT_BUTTON
    };

    static DirectWaitType wait_type = DIRECT_WAIT_NONE;
    static SDL_Keycode wait_key = SDLK_UNKNOWN;
    static SDL_JoystickID wait_device = -1;
    static int wait_button = -1;
    static int next_state = 13;

    // Wait for the just-captured control to be released, matching the behaviour
    // of the existing unified wizard and preventing one held button from being
    // assigned to multiple views.
    if (wait_type != DIRECT_WAIT_NONE)
    {
        draw_text("RELEASE CONTROL");

        bool released = false;

        if (wait_type == DIRECT_WAIT_KEYBOARD)
        {
            const Uint8* keyboard_state = SDL_GetKeyboardState(NULL);
            const SDL_Scancode scancode = SDL_GetScancodeFromKey(wait_key);

            released =
                scancode == SDL_SCANCODE_UNKNOWN ||
                keyboard_state[scancode] == 0;
        }
        else if (wait_type == DIRECT_WAIT_BUTTON)
        {
            const InputDevice* device = input.find_device(wait_device);

            released =
                device == nullptr ||
                device->joystick == nullptr ||
                wait_button < 0 ||
                wait_button >= device->buttons ||
                SDL_JoystickGetButton(device->joystick, wait_button) == 0;
        }

        if (released)
        {
            wait_type = DIRECT_WAIT_NONE;
            wait_key = SDLK_UNKNOWN;
            wait_device = -1;
            wait_button = -1;

            input.key_press = -1;
            input.joy_button = -1;
            input.joy_button_device = -1;
            input.reset_axis_config();

            redef_state = next_state;
        }

        return;
    }

    // Optional direct camera controls:
    // 13 = Original view, 14 = Elevated view, 15 = In-car/bumper view.
    if (redef_state >= 13 && redef_state <= 15)
    {
        const int index = redef_state - 13;
        const int key_slot = 12 + index;
        const int pad_slot = 15 + index;

        static const char* PROMPTS[3] =
        {
            "PRESS VIEW 1 - ENTER TO SKIP",
            "PRESS VIEW 2 - ENTER TO SKIP",
            "PRESS VIEW 3 - ENTER TO SKIP"
        };

        draw_text(PROMPTS[index]);

        // Enter skips this optional binding and leaves any existing assignment
        // unchanged.
        if (input.key_press == SDLK_RETURN)
        {
            wait_type = DIRECT_WAIT_KEYBOARD;
            wait_key = SDLK_RETURN;
            next_state = redef_state + 1;
            input.key_press = -1;
            return;
        }

        if (input.key_press != -1)
        {
            const SDL_Keycode captured_key = input.key_press;

            config.controls.keyconfig[key_slot] = captured_key;
            input.set_button_binding(pad_slot, -1, -1);

            wait_type = DIRECT_WAIT_KEYBOARD;
            wait_key = captured_key;
            next_state = redef_state + 1;
            input.key_press = -1;
            return;
        }

        if (input.joy_button != -1)
        {
            const int captured_button = input.joy_button;
            const SDL_JoystickID captured_device = input.joy_button_device;

            input.set_button_binding(
                pad_slot,
                captured_button,
                captured_device);

            config.controls.keyconfig[key_slot] = -1;

            wait_type = DIRECT_WAIT_BUTTON;
            wait_device = captured_device;
            wait_button = captured_button;
            next_state = redef_state + 1;

            input.joy_button = -1;
            input.joy_button_device = -1;
            return;
        }

        return;
    }

    if (redef_state >= 16)
    {
        wait_type = DIRECT_WAIT_NONE;
        wait_key = SDLK_UNKNOWN;
        wait_device = -1;
        wait_button = -1;

        input.key_press = -1;
        input.joy_button = -1;
        input.joy_button_device = -1;
        input.reset_axis_config();

        state = STATE_MENU;
        refresh_menu();
    }
}
