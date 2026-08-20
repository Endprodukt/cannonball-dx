/***************************************************************************
    SDL Based Input Handling - CannonBall-SE extensions.

    The existing multi-device implementation is retained in input_base.cpp.
    This wrapper adds optional direct buttons for the three enhanced views
    without changing the existing cycle-through VIEWPOINT control.
***************************************************************************/

#include "sdl2/input.hpp"

#define set_button_binding    set_button_binding_base
#define handle_key_down        handle_key_down_base
#define handle_key_up          handle_key_up_base
#define handle_joy_down        handle_joy_down_base
#define handle_joy_up          handle_joy_up_base
#define handle_controller_down handle_controller_down_base
#define handle_controller_up   handle_controller_up_base
#include "sdl2/input_base.cpp"
#undef set_button_binding
#undef handle_key_down
#undef handle_key_up
#undef handle_joy_down
#undef handle_joy_up
#undef handle_controller_down
#undef handle_controller_up

void Input::set_button_binding(int slot, int button, SDL_JoystickID device)
{
    if (slot < 0 || slot >= 18)
        return;

    pad_config[slot] = button;
    button_device[slot] = device;
}

void Input::handle_key_down(SDL_Keysym* keysym)
{
    handle_key_down_base(keysym);

    if (keysym->sym == key_config[12]) keys[VIEW1] = true;
    if (keysym->sym == key_config[13]) keys[VIEW2] = true;
    if (keysym->sym == key_config[14]) keys[VIEW3] = true;
}

void Input::handle_key_up(SDL_Keysym* keysym)
{
    handle_key_up_base(keysym);

    if (keysym->sym == key_config[12]) keys[VIEW1] = false;
    if (keysym->sym == key_config[13]) keys[VIEW2] = false;
    if (keysym->sym == key_config[14]) keys[VIEW3] = false;
}

void Input::handle_joy_down(SDL_JoyButtonEvent* evt)
{
    handle_joy_down_base(evt);

    auto matches = [&](int slot)
    {
        return evt->button == pad_config[slot] &&
               (button_device[slot] == -1 || button_device[slot] == evt->which);
    };

    if (matches(15)) keys[VIEW1] = true;
    if (matches(16)) keys[VIEW2] = true;
    if (matches(17)) keys[VIEW3] = true;
}

void Input::handle_joy_up(SDL_JoyButtonEvent* evt)
{
    handle_joy_up_base(evt);

    auto matches = [&](int slot)
    {
        return evt->button == pad_config[slot] &&
               (button_device[slot] == -1 || button_device[slot] == evt->which);
    };

    if (matches(15)) keys[VIEW1] = false;
    if (matches(16)) keys[VIEW2] = false;
    if (matches(17)) keys[VIEW3] = false;
}

void Input::handle_controller_down(SDL_ControllerButtonEvent* evt)
{
    handle_controller_down_base(evt);

    auto matches = [&](int slot)
    {
        return evt->button == pad_config[slot] &&
               (button_device[slot] == -1 || button_device[slot] == evt->which);
    };

    if (matches(15)) keys[VIEW1] = true;
    if (matches(16)) keys[VIEW2] = true;
    if (matches(17)) keys[VIEW3] = true;
}

void Input::handle_controller_up(SDL_ControllerButtonEvent* evt)
{
    handle_controller_up_base(evt);

    auto matches = [&](int slot)
    {
        return evt->button == pad_config[slot] &&
               (button_device[slot] == -1 || button_device[slot] == evt->which);
    };

    if (matches(15)) keys[VIEW1] = false;
    if (matches(16)) keys[VIEW2] = false;
    if (matches(17)) keys[VIEW3] = false;
}
