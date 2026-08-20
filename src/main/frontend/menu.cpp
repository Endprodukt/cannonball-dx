/***************************************************************************
    Front End Menu System - CannonBall-SE control-binding extensions.

    The existing menu implementation is retained in menu_base.cpp. Menu derives
    from MenuBase and replaces the sequential binding wizard with an editable
    keyboard / physical-device binding matrix.
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

namespace
{
    const int BINDING_ROWS = 12;

    const char* ROW_LABELS[BINDING_ROWS] =
    {
        "STEERING",
        "ACCELERATE",
        "BRAKE",
        "GEAR LOW",
        "GEAR HIGH",
        "START",
        "COIN",
        "MENU",
        "VIEW CHANGE",
        "VIEW 1",
        "VIEW 2",
        "VIEW 3",
    };

    const int ROW_TARGETS[BINDING_ROWS] =
    {
        device_binding_t::TARGET_STEER,
        device_binding_t::TARGET_ACCEL,
        device_binding_t::TARGET_BRAKE,
        device_binding_t::TARGET_GEAR1,
        device_binding_t::TARGET_GEAR2,
        device_binding_t::TARGET_START,
        device_binding_t::TARGET_COIN,
        device_binding_t::TARGET_MENU,
        device_binding_t::TARGET_VIEW,
        device_binding_t::TARGET_VIEW1,
        device_binding_t::TARGET_VIEW2,
        device_binding_t::TARGET_VIEW3,
    };

    // Steering is a two-key cell and therefore uses -1 here. The remaining
    // rows map directly to keyconfig[].
    const int ROW_KEY_SLOT[BINDING_ROWS] =
    {
        -1,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
    };

    std::string clip_text(const std::string& text, size_t width)
    {
        if (text.size() <= width)
            return text;

        return text.substr(0, width);
    }

    std::string compact_key_name(int key)
    {
        if (key < 0)
            return "-";

        switch (key)
        {
            case SDLK_UP:        return "UP";
            case SDLK_DOWN:      return "DOWN";
            case SDLK_LEFT:      return "LEFT";
            case SDLK_RIGHT:     return "RIGHT";
            case SDLK_RETURN:    return "ENTER";
            case SDLK_BACKSPACE: return "BSP";
            case SDLK_DELETE:    return "DEL";
            case SDLK_SPACE:     return "SPACE";
            case SDLK_ESCAPE:    return "ESC";
            default:
                break;
        }

        const char* name = SDL_GetKeyName(key);
        return (name && *name) ? std::string(name) : std::string("?");
    }

    std::string keyboard_binding_text(int row)
    {
        if (row == 0)
        {
            const std::string left = compact_key_name(config.controls.keyconfig[2]);
            const std::string right = compact_key_name(config.controls.keyconfig[3]);
            return clip_text(left + "/" + right, 8);
        }

        const int slot = ROW_KEY_SLOT[row];
        return clip_text(compact_key_name(config.controls.keyconfig[slot]), 8);
    }

    std::string hat_direction(int value)
    {
        if (value & SDL_HAT_UP)    return "U";
        if (value & SDL_HAT_DOWN)  return "D";
        if (value & SDL_HAT_LEFT)  return "L";
        if (value & SDL_HAT_RIGHT) return "R";
        return "?";
    }

    const device_binding_t* find_device_binding(
        int target,
        const std::string& signature)
    {
        const device_binding_t* wildcard = nullptr;

        for (const auto& binding : config.controls.device_bindings)
        {
            if (binding.target != target)
                continue;

            if (binding.device == signature)
                return &binding;

            if (binding.device == "*")
                wildcard = &binding;
        }

        return wildcard;
    }

    std::string physical_binding_text(
        int target,
        const std::string& signature)
    {
        const device_binding_t* binding =
            find_device_binding(target, signature);

        if (!binding)
            return "-";

        std::string text = binding->device == "*" ? "*" : "";

        switch (binding->type)
        {
            case device_binding_t::TYPE_AXIS:
                text += "AX" + std::to_string(binding->index);
                break;

            case device_binding_t::TYPE_HAT:
                text += "H" + std::to_string(binding->index) +
                    hat_direction(binding->value);
                break;

            default:
                text += "B" + std::to_string(binding->index);
                break;
        }

        return clip_text(text, 8);
    }
}

void Menu::redefine_joystick()
{
    enum WaitType
    {
        WAIT_NONE,
        WAIT_KEY,
        WAIT_BUTTON,
        WAIT_HAT,
    };

    static int selected_row = 0;
    static int selected_col = 0; // 0 = keyboard, 1..n = physical SDL device
    static bool capturing = false;
    static int steering_key_step = 0;

    static bool waiting_release = false;
    static bool capture_after_release = false;
    static WaitType wait_type = WAIT_NONE;
    static SDL_Keycode wait_key = SDLK_UNKNOWN;
    static SDL_JoystickID wait_device = -1;
    static int wait_button = -1;
    static int wait_hat = -1;
    static int wait_hat_value = SDL_HAT_CENTERED;

    const auto& devices = input.get_devices();

    auto clear_latches = [&]()
    {
        input.key_press = -1;
        input.joy_button = -1;
        input.joy_button_device = -1;
        input.joy_hat = -1;
        input.joy_hat_value = SDL_HAT_CENTERED;
        input.joy_hat_device = -1;
        input.reset_axis_config();
    };

    // menu_base.cpp sets redef_state to zero every time this editor is opened.
    if (redef_state == 0)
    {
        selected_row = 0;
        selected_col = 0;
        capturing = false;
        steering_key_step = 0;
        waiting_release = false;
        capture_after_release = false;
        wait_type = WAIT_NONE;
        clear_latches();
        redef_state = 1;
    }

    const int total_columns = 1 + static_cast<int>(devices.size());

    if (selected_col >= total_columns)
        selected_col = std::max(0, total_columns - 1);

    auto draw_editor = [&]()
    {
        ohud.blit_text_new(12, 2, "CONTROL BINDINGS", ohud.GREEN);

        ohud.blit_text_new(1, 4, "CONTROL", ohud.GREY);
        ohud.blit_text_new(
            14,
            4,
            "KEYBOARD",
            selected_col == 0 ? ohud.PINK : ohud.GREY);

        int selected_device_index = selected_col - 1;
        int first_device = 0;

        if (selected_device_index >= 2)
            first_device = selected_device_index - 1;

        if (static_cast<int>(devices.size()) > 2)
            first_device = std::min(first_device, static_cast<int>(devices.size()) - 2);

        const int device_x[2] = {23, 32};

        for (int visible = 0; visible < 2; visible++)
        {
            const int device_index = first_device + visible;
            if (device_index >= static_cast<int>(devices.size()))
                continue;

            const bool selected = selected_col == device_index + 1;
            const std::string name = clip_text(devices[device_index].name, 8);

            ohud.blit_text_new(
                device_x[visible],
                4,
                name.c_str(),
                selected ? ohud.PINK : ohud.GREY);
        }

        for (int row = 0; row < BINDING_ROWS; row++)
        {
            const int y = 6 + row;
            const bool selected_label = row == selected_row;

            ohud.blit_text_new(
                1,
                y,
                ROW_LABELS[row],
                selected_label ? ohud.PINK : ohud.GREEN);

            const std::string key_text = keyboard_binding_text(row);
            ohud.blit_text_new(
                14,
                y,
                key_text.c_str(),
                (row == selected_row && selected_col == 0) ? ohud.PINK : ohud.GREEN);

            for (int visible = 0; visible < 2; visible++)
            {
                const int device_index = first_device + visible;
                if (device_index >= static_cast<int>(devices.size()))
                    continue;

                const std::string signature =
                    input.get_device_signature(devices[device_index].instance_id);

                const std::string binding =
                    physical_binding_text(ROW_TARGETS[row], signature);

                ohud.blit_text_new(
                    device_x[visible],
                    y,
                    binding.c_str(),
                    (row == selected_row && selected_col == device_index + 1)
                        ? ohud.PINK
                        : ohud.GREEN);
            }
        }

        if (waiting_release)
        {
            ohud.blit_text_new(11, 20, "RELEASE CONTROL", ohud.PINK);
        }
        else if (capturing)
        {
            if (selected_col == 0 && selected_row == 0)
            {
                ohud.blit_text_new(
                    4,
                    20,
                    steering_key_step == 0 ? "PRESS STEERING LEFT KEY" : "PRESS STEERING RIGHT KEY",
                    ohud.PINK);
            }
            else if (selected_col == 0)
            {
                ohud.blit_text_new(8, 20, "PRESS A KEY", ohud.PINK);
            }
            else if (selected_row == 0)
            {
                ohud.blit_text_new(5, 20, "MOVE STEERING AXIS", ohud.PINK);
            }
            else if (selected_row == 1 || selected_row == 2)
            {
                ohud.blit_text_new(2, 20, "MOVE AXIS OR PRESS BUTTON", ohud.PINK);
            }
            else
            {
                ohud.blit_text_new(5, 20, "PRESS BUTTON OR HAT", ohud.PINK);
            }
        }
        else
        {
            ohud.blit_text_new(1, 20, "ARROWS: SELECT   ENTER: CHANGE", ohud.GREY);
            ohud.blit_text_new(1, 21, "DEL/BSP: CLEAR   MENU: BACK", ohud.GREY);

            if (devices.size() > 2)
                ohud.blit_text_new(1, 22, "LEFT/RIGHT SCROLLS DEVICES", ohud.GREY);
        }
    };

    // Wait only for the control that opened the cell or was just captured.
    if (waiting_release)
    {
        bool released = false;

        if (wait_type == WAIT_KEY)
        {
            const Uint8* keyboard_state = SDL_GetKeyboardState(nullptr);
            const SDL_Scancode scancode = SDL_GetScancodeFromKey(wait_key);

            released =
                scancode == SDL_SCANCODE_UNKNOWN ||
                keyboard_state[scancode] == 0;
        }
        else if (wait_type == WAIT_BUTTON)
        {
            const InputDevice* device = input.find_device(wait_device);

            released =
                device == nullptr ||
                device->joystick == nullptr ||
                wait_button < 0 ||
                wait_button >= device->buttons ||
                SDL_JoystickGetButton(device->joystick, wait_button) == 0;
        }
        else if (wait_type == WAIT_HAT)
        {
            const InputDevice* device = input.find_device(wait_device);

            if (device == nullptr ||
                device->joystick == nullptr ||
                wait_hat < 0 ||
                wait_hat >= device->hats)
            {
                released = true;
            }
            else
            {
                const Uint8 value = SDL_JoystickGetHat(device->joystick, wait_hat);
                released = (value & wait_hat_value) == 0;
            }
        }
        else
        {
            released = true;
        }

        if (released)
        {
            waiting_release = false;
            wait_type = WAIT_NONE;
            wait_key = SDLK_UNKNOWN;
            wait_device = -1;
            wait_button = -1;
            wait_hat = -1;
            wait_hat_value = SDL_HAT_CENTERED;
            clear_latches();

            capturing = capture_after_release;
            capture_after_release = false;
        }

        draw_editor();
        return;
    }

    // Capture the selected cell. The key/button used to enter this state was
    // released first, so Enter itself is now a perfectly valid binding.
    if (capturing)
    {
        if (selected_col == 0)
        {
            if (input.key_press != -1)
            {
                const SDL_Keycode captured_key = input.key_press;

                if (selected_row == 0)
                {
                    const int slot = steering_key_step == 0 ? 2 : 3;
                    config.controls.keyconfig[slot] = captured_key;

                    steering_key_step++;
                    capture_after_release = steering_key_step < 2;

                    if (!capture_after_release)
                        steering_key_step = 0;
                }
                else
                {
                    config.controls.keyconfig[ROW_KEY_SLOT[selected_row]] = captured_key;
                    capture_after_release = false;
                }

                waiting_release = true;
                wait_type = WAIT_KEY;
                wait_key = captured_key;
                input.key_press = -1;
            }

            draw_editor();
            return;
        }

        const int device_index = selected_col - 1;
        if (device_index < 0 || device_index >= static_cast<int>(devices.size()))
        {
            capturing = false;
            draw_editor();
            return;
        }

        const SDL_JoystickID selected_device =
            devices[device_index].instance_id;

        const int target = ROW_TARGETS[selected_row];

        // Steering accepts an analog axis. Accelerator and brake accept either
        // an axis or a digital button/HAT. Other rows are digital controls.
        if (selected_row <= 2)
        {
            SDL_JoystickID captured_device = -1;
            const int axis = input.get_axis_config(&captured_device);

            if (axis != -1)
            {
                if (captured_device == selected_device)
                {
                    input.set_device_binding(
                        target,
                        device_binding_t::TYPE_AXIS,
                        axis,
                        0,
                        selected_device);

                    capturing = false;
                    clear_latches();
                }

                draw_editor();
                return;
            }
        }

        if (selected_row != 0 &&
            input.joy_hat != -1 &&
            input.joy_hat_value != SDL_HAT_CENTERED &&
            input.joy_hat_device == selected_device)
        {
            const int captured_hat = input.joy_hat;
            const int captured_value = input.joy_hat_value;

            input.set_device_binding(
                target,
                device_binding_t::TYPE_HAT,
                captured_hat,
                captured_value,
                selected_device);

            capturing = false;
            waiting_release = true;
            capture_after_release = false;
            wait_type = WAIT_HAT;
            wait_device = selected_device;
            wait_hat = captured_hat;
            wait_hat_value = captured_value;

            input.joy_hat = -1;
            input.joy_hat_value = SDL_HAT_CENTERED;
            input.joy_hat_device = -1;

            draw_editor();
            return;
        }

        if (selected_row != 0 &&
            input.joy_button != -1 &&
            input.joy_button_device == selected_device)
        {
            const int captured_button = input.joy_button;

            input.set_device_binding(
                target,
                device_binding_t::TYPE_BUTTON,
                captured_button,
                0,
                selected_device);

            capturing = false;
            waiting_release = true;
            capture_after_release = false;
            wait_type = WAIT_BUTTON;
            wait_device = selected_device;
            wait_button = captured_button;

            input.joy_button = -1;
            input.joy_button_device = -1;

            draw_editor();
            return;
        }

        draw_editor();
        return;
    }

    // Browse mode: move freely through the matrix and edit just one cell.
    if (input.has_pressed(Input::MENU))
    {
        clear_latches();
        redef_state = 0;
        state = STATE_MENU;
        refresh_menu();
        return;
    }

    if (input.has_pressed(Input::DOWN))
    {
        selected_row++;
        if (selected_row >= BINDING_ROWS)
            selected_row = 0;

        osoundint.queue_sound(sound::BEEP1);
    }
    else if (input.has_pressed(Input::UP))
    {
        selected_row--;
        if (selected_row < 0)
            selected_row = BINDING_ROWS - 1;

        osoundint.queue_sound(sound::BEEP1);
    }
    else if (input.has_pressed(Input::RIGHT))
    {
        selected_col++;
        if (selected_col >= total_columns)
            selected_col = 0;

        osoundint.queue_sound(sound::BEEP1);
    }
    else if (input.has_pressed(Input::LEFT))
    {
        selected_col--;
        if (selected_col < 0)
            selected_col = total_columns - 1;

        osoundint.queue_sound(sound::BEEP1);
    }

    const bool clear_pressed =
        input.key_press == SDLK_DELETE ||
        input.key_press == SDLK_BACKSPACE;

    if (clear_pressed)
    {
        if (selected_col == 0)
        {
            if (selected_row == 0)
            {
                config.controls.keyconfig[2] = -1;
                config.controls.keyconfig[3] = -1;
            }
            else
            {
                config.controls.keyconfig[ROW_KEY_SLOT[selected_row]] = -1;
            }
        }
        else
        {
            const int device_index = selected_col - 1;

            if (device_index >= 0 && device_index < static_cast<int>(devices.size()))
            {
                input.clear_device_binding(
                    ROW_TARGETS[selected_row],
                    devices[device_index].instance_id);
            }
        }

        input.key_press = -1;
        osoundint.queue_sound(sound::BEEP1);
    }

    const bool activate =
        input.key_press == SDLK_RETURN ||
        select_pressed();

    if (activate)
    {
        capturing = false;
        steering_key_step = 0;
        capture_after_release = true;

        if (input.joy_button != -1)
        {
            waiting_release = true;
            wait_type = WAIT_BUTTON;
            wait_device = input.joy_button_device;
            wait_button = input.joy_button;
        }
        else if (input.key_press != -1)
        {
            waiting_release = true;
            wait_type = WAIT_KEY;
            wait_key = input.key_press;
        }
        else
        {
            // Analog cabinet select has no discrete control to release.
            capturing = true;
            capture_after_release = false;
        }

        input.key_press = -1;
        input.joy_button = -1;
        input.joy_button_device = -1;
        input.joy_hat = -1;
        input.joy_hat_value = SDL_HAT_CENTERED;
        input.joy_hat_device = -1;
        input.reset_axis_config();
    }

    draw_editor();
}
