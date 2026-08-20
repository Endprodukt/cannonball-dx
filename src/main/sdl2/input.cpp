/***************************************************************************
    SDL Based Input Handling - CannonBall-SE extensions.

    The existing multi-device implementation is retained in input_base.cpp.
    This wrapper adds optional direct buttons for the three enhanced views and
    persistent per-device bindings used by the control-binding matrix.
***************************************************************************/

#include "sdl2/input.hpp"

#define set_button_binding     set_button_binding_base
#define handle_key_down        handle_key_down_base
#define handle_key_up          handle_key_up_base
#define handle_joy_axis        handle_joy_axis_base
#define handle_joy_down        handle_joy_down_base
#define handle_joy_up          handle_joy_up_base
#define handle_joy_hat         handle_joy_hat_base
#define handle_controller_axis handle_controller_axis_base
#define handle_controller_down handle_controller_down_base
#define handle_controller_up   handle_controller_up_base
#include "sdl2/input_base.cpp"
#undef set_button_binding
#undef handle_key_down
#undef handle_key_up
#undef handle_joy_axis
#undef handle_joy_down
#undef handle_joy_up
#undef handle_joy_hat
#undef handle_controller_axis
#undef handle_controller_down
#undef handle_controller_up

const std::vector<InputDevice>& Input::get_devices() const
{
    return devices;
}

std::string Input::get_device_signature(SDL_JoystickID device) const
{
    const InputDevice* input_device = find_device(device);
    return input_device ? make_device_signature(*input_device) : std::string();
}

void Input::set_device_binding(
    int target,
    int type,
    int index,
    int value,
    SDL_JoystickID device)
{
    if (target < device_binding_t::TARGET_STEER ||
        target > device_binding_t::TARGET_VIEW3 ||
        type < device_binding_t::TYPE_BUTTON ||
        type > device_binding_t::TYPE_HAT ||
        index < 0)
    {
        return;
    }

    const std::string signature = get_device_signature(device);
    if (signature.empty())
        return;

    auto& bindings = config.controls.device_bindings;

    // A concrete device assignment supersedes an old wildcard assignment for
    // this action, and replaces the previous assignment in this exact cell.
    bindings.erase(
        std::remove_if(
            bindings.begin(),
            bindings.end(),
            [&](const device_binding_t& binding)
            {
                return binding.target == target &&
                    (binding.device == "*" || binding.device == signature);
            }),
        bindings.end());

    device_binding_t binding;
    binding.target = target;
    binding.type = type;
    binding.index = index;
    binding.value = value;
    binding.device = signature;
    bindings.push_back(binding);
}

void Input::clear_device_binding(int target, SDL_JoystickID device)
{
    const std::string signature = get_device_signature(device);
    if (signature.empty())
        return;

    auto& bindings = config.controls.device_bindings;

    bindings.erase(
        std::remove_if(
            bindings.begin(),
            bindings.end(),
            [&](const device_binding_t& binding)
            {
                return binding.target == target &&
                    (binding.device == signature || binding.device == "*");
            }),
        bindings.end());
}

void Input::set_button_binding(int slot, int button, SDL_JoystickID device)
{
    if (slot < 0 || slot >= 18)
        return;

    pad_config[slot] = button;
    button_device[slot] = device;
}

void Input::set_device_target(int target, bool is_pressed)
{
    switch (target)
    {
        case device_binding_t::TARGET_ACCEL:
            keys[ACCEL] = is_pressed;
            break;

        case device_binding_t::TARGET_BRAKE:
            keys[BRAKE] = is_pressed;
            break;

        case device_binding_t::TARGET_GEAR1:
            keys[GEAR1] = is_pressed;
            break;

        case device_binding_t::TARGET_GEAR2:
            keys[GEAR2] = is_pressed;
            break;

        case device_binding_t::TARGET_START:
            keys[START] = is_pressed;
            break;

        case device_binding_t::TARGET_COIN:
            keys[COIN] = is_pressed;
            break;

        case device_binding_t::TARGET_MENU:
            keys[MENU] = is_pressed;
            break;

        case device_binding_t::TARGET_VIEW:
            keys[VIEWPOINT] = is_pressed;
            break;

        case device_binding_t::TARGET_VIEW1:
            keys[VIEW1] = is_pressed;
            break;

        case device_binding_t::TARGET_VIEW2:
            keys[VIEW2] = is_pressed;
            break;

        case device_binding_t::TARGET_VIEW3:
            keys[VIEW3] = is_pressed;
            break;

        default:
            break;
    }
}

void Input::apply_device_button(
    SDL_JoystickID device,
    int button,
    bool is_pressed)
{
    const std::string signature = get_device_signature(device);
    if (signature.empty())
        return;

    for (const auto& binding : config.controls.device_bindings)
    {
        if (binding.type != device_binding_t::TYPE_BUTTON ||
            binding.index != button ||
            (binding.device != "*" && binding.device != signature))
        {
            continue;
        }

        set_device_target(binding.target, is_pressed);
    }
}

void Input::apply_device_hat(
    SDL_JoystickID device,
    int hat,
    int value)
{
    const std::string signature = get_device_signature(device);
    if (signature.empty())
        return;

    for (const auto& binding : config.controls.device_bindings)
    {
        if (binding.type != device_binding_t::TYPE_HAT ||
            binding.index != hat ||
            (binding.device != "*" && binding.device != signature))
        {
            continue;
        }

        const bool pressed =
            binding.value != SDL_HAT_CENTERED &&
            (value & binding.value) != 0;

        set_device_target(binding.target, pressed);
    }
}

void Input::apply_device_axis(
    SDL_JoystickID device,
    int ax,
    int value)
{
    if (!analog)
        return;

    const std::string signature = get_device_signature(device);
    if (signature.empty())
        return;

    for (const auto& binding : config.controls.device_bindings)
    {
        if (binding.type != device_binding_t::TYPE_AXIS ||
            binding.index != ax ||
            (binding.device != "*" && binding.device != signature))
        {
            continue;
        }

        if (binding.target == device_binding_t::TARGET_STEER)
        {
            int adjusted = value;

            if (wheel_zone && wheel_zone < 100)
                adjusted = adjusted / (100 - wheel_zone);

            adjusted = ((adjusted + 0x8000) / 0x200);
            adjusted += 0x40;

            if (adjusted < 0x40)
                adjusted = 0x40;
            else if (adjusted > 0xC0)
                adjusted = 0xC0;

            if (wheel_dead &&
                std::abs(CENTRE - adjusted) <= wheel_dead)
            {
                adjusted = CENTRE;
            }

            a_wheel = adjusted;
        }
        else if (binding.target == device_binding_t::TARGET_ACCEL ||
                 binding.target == device_binding_t::TARGET_BRAKE)
        {
            const int invert_slot =
                binding.target == device_binding_t::TARGET_ACCEL ? 1 : 2;

            int working = invert[invert_slot] ? -value : value;
            int scaled = 0;

            // Scale per originating device rather than relying on the one
            // global SDL_GameController pointer. This matters when a wheel and
            // a gamepad are connected at the same time.
            if (SDL_GameControllerFromInstanceID(device) != nullptr)
                scaled = working / 0x80;
            else
                scaled = (working + 0x8000) / 0x100;

            if (scaled < 0)
                scaled = 0;
            else if (scaled > 0xFF)
                scaled = 0xFF;

            if (binding.target == device_binding_t::TARGET_ACCEL)
                a_accel = scaled;
            else
                a_brake = scaled;
        }
    }
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

void Input::handle_joy_axis(SDL_JoyAxisEvent* evt)
{
    // SDL can emit both JOY and CONTROLLER events for the same device when it
    // has been opened as an SDL_GameController. Ignore only that device's raw
    // duplicate event. Other raw devices (wheels, shifters, joysticks, button
    // boxes, etc.) must remain fully active even while a gamepad is connected.
    if (SDL_GameControllerFromInstanceID(evt->which) != nullptr)
        return;

    // Do not use handle_joy_axis_base() here: the legacy implementation checks
    // the one global controller pointer and would reject every raw joystick as
    // soon as any GameController is open.
    handle_axis(evt->which, evt->axis, evt->value);

    // handle_axis only runs capture logic while analog mode is enabled. The
    // binding editor must be able to discover axes regardless of that setting.
    if (!analog)
        store_last_axis(evt->which, evt->axis, evt->value);

    apply_device_axis(evt->which, evt->axis, evt->value);
}

void Input::handle_controller_axis(SDL_ControllerAxisEvent* evt)
{
    handle_controller_axis_base(evt);

    if (!analog)
        store_last_axis(evt->which, evt->axis, evt->value);

    apply_device_axis(evt->which, evt->axis, evt->value);
}

void Input::handle_joy_down(SDL_JoyButtonEvent* evt)
{
    // See handle_joy_axis(): suppress only the raw duplicate belonging to an
    // actually opened SDL_GameController, never every joystick globally.
    if (SDL_GameControllerFromInstanceID(evt->which) != nullptr)
        return;

    joy_button = evt->button;
    joy_button_device = evt->which;

    // Preserve legacy button behaviour for raw devices and also feed the new
    // per-device binding layer.
    handle_joy(evt->which, evt->button, true);
    apply_device_button(evt->which, evt->button, true);

    // Legacy direct-view fallback. New configs normally migrate these slots to
    // device_bindings, but keeping this makes older in-memory setups harmless.
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
    if (SDL_GameControllerFromInstanceID(evt->which) != nullptr)
        return;

    if (joy_button == evt->button &&
        joy_button_device == evt->which)
    {
        joy_button = -1;
        joy_button_device = -1;
    }

    handle_joy(evt->which, evt->button, false);
    apply_device_button(evt->which, evt->button, false);

    auto matches = [&](int slot)
    {
        return evt->button == pad_config[slot] &&
               (button_device[slot] == -1 || button_device[slot] == evt->which);
    };

    if (matches(15)) keys[VIEW1] = false;
    if (matches(16)) keys[VIEW2] = false;
    if (matches(17)) keys[VIEW3] = false;
}

void Input::handle_joy_hat(SDL_JoyHatEvent* evt)
{
    // D-pads from opened GameControllers arrive through controller button
    // events. Ignore only their duplicate raw HAT event; HATs on wheels and
    // generic joysticks remain available for binding.
    if (SDL_GameControllerFromInstanceID(evt->which) != nullptr)
        return;

    handle_joy_hat_base(evt);
    apply_device_hat(evt->which, evt->hat, evt->value);
}

void Input::handle_controller_down(SDL_ControllerButtonEvent* evt)
{
    handle_controller_down_base(evt);
    apply_device_button(evt->which, evt->button, true);

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
    apply_device_button(evt->which, evt->button, false);

    auto matches = [&](int slot)
    {
        return evt->button == pad_config[slot] &&
               (button_device[slot] == -1 || button_device[slot] == evt->which);
    };

    if (matches(15)) keys[VIEW1] = false;
    if (matches(16)) keys[VIEW2] = false;
    if (matches(17)) keys[VIEW3] = false;
}
