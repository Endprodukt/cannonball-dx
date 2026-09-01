/***************************************************************************
    SDL Based Input Handling - CannonBall-SE extensions.

    The existing multi-device implementation is retained in input_base.cpp.
    This wrapper adds optional direct buttons for the three enhanced views and
    persistent logical GAMEPAD/WHEEL binding groups used by the binding matrix.
***************************************************************************/

#include "sdl2/input.hpp"
#include "sdl2/gamepad_rumble_state.hpp"

#define scan_joysticks        scan_joysticks_base
#define add_joystick          add_joystick_base
#define remove_joystick       remove_joystick_base
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
#define reset_axis_config      reset_axis_config_base
#define set_rumble             set_rumble_base
#include "sdl2/input_base.cpp"
#undef scan_joysticks
#undef add_joystick
#undef remove_joystick
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
#undef reset_axis_config
#undef set_rumble

namespace
{
    const char* GAMEPAD_PREFIX = "G:";
    const char* WHEEL_PREFIX = "W:";

    SDL_Keycode display_toggle_key = SDLK_UNKNOWN;
    int last_fullscreen_mode = video_settings_t::MODE_FULL;

    bool is_display_toggle(const SDL_Keysym* keysym)
    {
        if (!keysym)
            return false;

        if (keysym->sym == SDLK_F11)
            return true;

        const bool enter =
            keysym->sym == SDLK_RETURN || keysym->sym == SDLK_KP_ENTER;
        return enter && (keysym->mod & KMOD_ALT) != 0;
    }

    void toggle_display_mode()
    {
        const int current = config.video.mode;

        if (current == video_settings_t::MODE_WINDOW)
        {
            if (last_fullscreen_mode != video_settings_t::MODE_FULL &&
                last_fullscreen_mode != video_settings_t::MODE_STRETCH)
            {
                last_fullscreen_mode = video_settings_t::MODE_FULL;
            }

            config.video.mode = last_fullscreen_mode;
        }
        else
        {
            if (current == video_settings_t::MODE_FULL ||
                current == video_settings_t::MODE_STRETCH)
            {
                last_fullscreen_mode = current;
            }
            else
            {
                last_fullscreen_mode = video_settings_t::MODE_FULL;
            }

            config.video.mode = video_settings_t::MODE_WINDOW;
        }

        config.videoRestartRequired = true;

        // Hotkeys operate outside the frontend save latch, so persist the new
        // mode immediately just like the existing F-key enhancement toggles.
        if (!config.save())
            std::cerr << "Unable to save display mode setting." << std::endl;
    }

    bool has_group_prefix(const std::string& device)
    {
        return device.size() >= 2 &&
            (device.compare(0, 2, GAMEPAD_PREFIX) == 0 ||
             device.compare(0, 2, WHEEL_PREFIX) == 0);
    }

    std::string raw_binding_signature(const std::string& device)
    {
        if (has_group_prefix(device))
            return device.substr(2);

        return device;
    }

    bool binding_matches_signature(
        const std::string& stored_device,
        const std::string& signature)
    {
        const std::string stored_signature =
            raw_binding_signature(stored_device);

        return stored_signature == "*" || stored_signature == signature;
    }

    bool binding_is_group(const std::string& device, int group)
    {
        if (group == Input::BINDING_GAMEPAD)
            return device.compare(0, 2, GAMEPAD_PREFIX) == 0;

        return device.compare(0, 2, WHEEL_PREFIX) == 0;
    }

    bool binding_matches_group_signature(
        const std::string& stored_device,
        const std::string& signature,
        int group)
    {
        if (!binding_is_group(stored_device, group))
            return false;

        const std::string stored_signature =
            raw_binding_signature(stored_device);

        // Wildcards came from the old single-pad configuration where the
        // physical device was not stored. They are unsafe in the new grouped
        // input model because the same physical gamepad also emits raw joystick
        // events with different button numbers. Never execute them.
        if (stored_signature == "*")
            return false;

        return stored_signature == signature;
    }

    bool group_has_axis_target(int target, int group)
    {
        for (const auto& binding : config.controls.device_bindings)
        {
            if (binding.target == target &&
                binding.type == device_binding_t::TYPE_AXIS &&
                binding_is_group(binding.device, group))
            {
                return true;
            }
        }

        return false;
    }

    bool any_matrix_axis_target(int target)
    {
        return
            group_has_axis_target(target, Input::BINDING_GAMEPAD) ||
            group_has_axis_target(target, Input::BINDING_WHEEL);
    }
}

void Input::ensure_gamecontroller_open()
{
    if (controller && SDL_GameControllerGetAttached(controller))
        return;

    if (controller)
    {
        SDL_GameControllerClose(controller);
        controller = nullptr;
        rumble_supported = false;
    }

    const int count = SDL_NumJoysticks();

    for (int i = 0; i < count; i++)
    {
        if (!SDL_IsGameController(i))
            continue;

        SDL_GameController* candidate = SDL_GameControllerOpen(i);
        if (!candidate)
            continue;

        controller = candidate;
        gamepad = true;
        rumble_supported =
            SDL_GameControllerRumble(controller, 0, 0, 0) == 0;

        const char* name = SDL_GameControllerName(controller);
        std::cout
            << "GameController input enabled: "
            << (name ? name : "Unknown GameController")
            << (rumble_supported ? " with rumble" : " without rumble")
            << std::endl;

        return;
    }
}

void Input::scan_joysticks()
{
    scan_joysticks_base();
    ensure_gamecontroller_open();
    normalize_device_bindings();
}

void Input::add_joystick(int device_index)
{
    add_joystick_base(device_index);
    ensure_gamecontroller_open();
    normalize_device_bindings();
}

void Input::remove_joystick(SDL_JoystickID instance_id)
{
    if (controller)
    {
        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);

        if (joystick && SDL_JoystickInstanceID(joystick) == instance_id)
        {
            SDL_GameControllerRumble(controller, 0, 0, 0);
            SDL_GameControllerClose(controller);
            controller = nullptr;
            rumble_supported = false;
        }
    }

    remove_joystick_base(instance_id);
    ensure_gamecontroller_open();
    normalize_device_bindings();
}

const std::vector<InputDevice>& Input::get_devices() const
{
    return devices;
}

std::string Input::get_device_signature(SDL_JoystickID device) const
{
    const InputDevice* input_device = find_device(device);
    return input_device ? make_device_signature(*input_device) : std::string();
}

SDL_JoystickID Input::get_gamepad_device() const
{
    // Do not depend on the one legacy controller pointer here. With several
    // physical devices connected (wheel, pedals, shifter, gamepad), pad_id can
    // refer to a raw joystick and controller may therefore be null even though
    // SDL has a perfectly valid GameController connected.
    const int count = SDL_NumJoysticks();

    for (int i = 0; i < count; i++)
    {
        if (!SDL_IsGameController(i))
            continue;

        const SDL_JoystickID instance_id =
            SDL_JoystickGetDeviceInstanceID(i);

        if (instance_id >= 0)
            return instance_id;
    }

    // Fallback for unusual backends where the controller is open but no longer
    // appears in the current device-index scan.
    if (controller)
    {
        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
        if (joystick)
            return SDL_JoystickInstanceID(joystick);
    }

    return -1;
}

void Input::normalize_device_bindings()
{
    const SDL_JoystickID gamepad_device = get_gamepad_device();
    const std::string gamepad_signature =
        get_device_signature(gamepad_device);

    auto& bindings = config.controls.device_bindings;

    // The old single-pad configuration did not remember a physical device.
    // Those wildcard bindings cannot be mapped safely because SDL raw joystick
    // button numbers and SDL GameController button numbers are not the same.
    bindings.erase(
        std::remove_if(
            bindings.begin(),
            bindings.end(),
            [&](const device_binding_t& binding)
            {
                return
                    binding.device == "*" ||
                    binding.device == "W:*" ||
                    binding.device == "G:*";
            }),
        bindings.end());

    for (auto& binding : bindings)
    {
        if (has_group_prefix(binding.device))
            continue;

        if (!gamepad_signature.empty() &&
            binding.device == gamepad_signature)
        {
            binding.device = std::string(GAMEPAD_PREFIX) + binding.device;
        }
        else
        {
            binding.device = std::string(WHEEL_PREFIX) + binding.device;
        }
    }

    // Every matrix cell represents exactly one logical assignment. Older
    // versions allowed WHEEL bindings to accumulate and could therefore show
    // MULTI after rebinding. Keep only the most recently stored binding for
    // each target in each logical column.
    std::vector<int> last_gamepad_binding(
        device_binding_t::TARGET_VIEW3 + 1,
        -1);
    std::vector<int> last_wheel_binding(
        device_binding_t::TARGET_VIEW3 + 1,
        -1);

    for (int i = 0; i < static_cast<int>(bindings.size()); i++)
    {
        const auto& binding = bindings[i];
        if (binding.target < device_binding_t::TARGET_STEER ||
            binding.target > device_binding_t::TARGET_VIEW3)
        {
            continue;
        }

        if (binding_is_group(binding.device, BINDING_GAMEPAD))
            last_gamepad_binding[binding.target] = i;
        else if (binding_is_group(binding.device, BINDING_WHEEL))
            last_wheel_binding[binding.target] = i;
    }

    std::vector<device_binding_t> normalized;
    normalized.reserve(bindings.size());

    for (int i = 0; i < static_cast<int>(bindings.size()); i++)
    {
        const auto& binding = bindings[i];

        if (binding.target >= device_binding_t::TARGET_STEER &&
            binding.target <= device_binding_t::TARGET_VIEW3)
        {
            if (binding_is_group(binding.device, BINDING_GAMEPAD) &&
                last_gamepad_binding[binding.target] != i)
            {
                continue;
            }

            if (binding_is_group(binding.device, BINDING_WHEEL) &&
                last_wheel_binding[binding.target] != i)
            {
                continue;
            }
        }

        normalized.push_back(binding);
    }

    bindings.swap(normalized);
}

void Input::set_device_binding(
    int target,
    int type,
    int index,
    int value,
    SDL_JoystickID device,
    int group)
{
    if (target < device_binding_t::TARGET_STEER ||
        target > device_binding_t::TARGET_VIEW3 ||
        type < device_binding_t::TYPE_BUTTON ||
        type > device_binding_t::TYPE_HAT ||
        index < 0 ||
        (group != BINDING_GAMEPAD && group != BINDING_WHEEL))
    {
        return;
    }

    const SDL_JoystickID gamepad_device = get_gamepad_device();

    // GAMEPAD bindings use SDL's standardized controller numbering and must
    // therefore belong to the active GameController. WHEEL deliberately uses
    // the raw joystick side and may target that very same physical device.
    if (group == BINDING_GAMEPAD && device != gamepad_device)
        return;

    const std::string signature = get_device_signature(device);
    if (signature.empty())
        return;

    const std::string stored_device =
        std::string(group == BINDING_GAMEPAD ? GAMEPAD_PREFIX : WHEEL_PREFIX) +
        signature;

    auto& bindings = config.controls.device_bindings;

    // A matrix cell is a single assignment. Rebinding always replaces the
    // previous button, hat or axis for that target in the selected column.
    bindings.erase(
        std::remove_if(
            bindings.begin(),
            bindings.end(),
            [&](const device_binding_t& binding)
            {
                return binding.target == target &&
                    binding_is_group(binding.device, group);
            }),
        bindings.end());

    device_binding_t binding;
    binding.target = target;
    binding.type = type;
    binding.index = index;
    binding.value = value;
    binding.device = stored_device;
    bindings.push_back(binding);
}

void Input::set_capture_group(int group)
{
    if (group == BINDING_GAMEPAD || group == BINDING_WHEEL)
        capture_group = group;
    else
        capture_group = -1;
}

void Input::clear_device_bindings(int target, int group)
{
    auto& bindings = config.controls.device_bindings;

    bindings.erase(
        std::remove_if(
            bindings.begin(),
            bindings.end(),
            [&](const device_binding_t& binding)
            {
                return binding.target == target &&
                    binding_is_group(binding.device, group);
            }),
        bindings.end());
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
                    binding_matches_signature(binding.device, signature);
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
    bool is_pressed,
    int group)
{
    const std::string signature = get_device_signature(device);
    if (signature.empty())
        return;

    for (const auto& binding : config.controls.device_bindings)
    {
        if (binding.type != device_binding_t::TYPE_BUTTON ||
            binding.index != button ||
            !binding_matches_group_signature(binding.device, signature, group))
        {
            continue;
        }

        set_device_target(binding.target, is_pressed);
    }
}

void Input::apply_device_hat(
    SDL_JoystickID device,
    int hat,
    int value,
    int group)
{
    const std::string signature = get_device_signature(device);
    if (signature.empty())
        return;

    for (const auto& binding : config.controls.device_bindings)
    {
        if (binding.type != device_binding_t::TYPE_HAT ||
            binding.index != hat ||
            !binding_matches_group_signature(binding.device, signature, group))
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
    int value,
    int group)
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
            !binding_matches_group_signature(binding.device, signature, group))
        {
            continue;
        }

        if (binding.target == device_binding_t::TARGET_STEER)
        {
            int raw = value;

            // wheel_zone is a WHEEL saturation setting, not a gamepad-stick
            // setting. Applying the default 75% wheel zone to a GameController
            // makes a thumbstick far too sensitive. Only raw wheel devices use
            // the saturation boost.
            if (group == BINDING_WHEEL &&
                wheel_zone > 0 && wheel_zone < 100)
            {
                raw = (raw * 100) / (100 - wheel_zone);
            }

            if (raw < SDL_JOYSTICK_AXIS_MIN)
                raw = SDL_JOYSTICK_AXIS_MIN;
            else if (raw > SDL_JOYSTICK_AXIS_MAX)
                raw = SDL_JOYSTICK_AXIS_MAX;

            // Map the signed SDL range exactly to OutRun's 0x40..0xC0 range.
            // This keeps 0 exactly centred and allows both endpoints to reach
            // the game's full steering limits symmetrically.
            int adjusted = CENTRE;

            if (raw >= 0)
                adjusted += (raw * 0x40) / SDL_JOYSTICK_AXIS_MAX;
            else
                adjusted += (raw * 0x40) / 0x8000;

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
            int scaled =
                group == BINDING_GAMEPAD
                    ? working / 0x80
                    : (working + 0x8000) / 0x100;

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

void Input::reset_axis_config()
{
    reset_axis_config_base();

    // Snapshot every raw axis at the moment a new cell starts listening. Axis
    // detection is then based on movement away from that position instead of
    // assuming a particular centre/rest value. This is important for pedals.
    axis_capture_baseline.clear();
    SDL_JoystickUpdate();

    for (const auto& device : devices)
    {
        if (!device.joystick)
            continue;

        for (int ax = 0; ax < device.axes; ax++)
        {
            AxisCaptureBaseline baseline;
            baseline.device = device.instance_id;
            baseline.axis = ax;
            baseline.value = SDL_JoystickGetAxis(device.joystick, ax);
            axis_capture_baseline.push_back(baseline);
        }
    }
}

void Input::capture_raw_axis_motion(
    SDL_JoystickID device,
    const uint8_t ax,
    const int16_t value)
{
    if (capture_group != BINDING_WHEEL)
        return;

    const int threshold = SDL_JOYSTICK_AXIS_MAX / 5;

    for (const auto& baseline : axis_capture_baseline)
    {
        if (baseline.device != device || baseline.axis != ax)
            continue;

        if (std::abs(static_cast<int>(value) - baseline.value) >= threshold)
        {
            axis_config = ax;
            axis_config_device = device;
            axis_counter = 2;
        }
        return;
    }

    // A device/axis may have appeared after the snapshot. Remember its current
    // value; the next real movement can then be detected normally.
    AxisCaptureBaseline baseline;
    baseline.device = device;
    baseline.axis = ax;
    baseline.value = value;
    axis_capture_baseline.push_back(baseline);
}

void Input::handle_key_down(SDL_Keysym* keysym)
{
    if (is_display_toggle(keysym))
    {
        if (display_toggle_key == SDLK_UNKNOWN)
        {
            display_toggle_key = keysym->sym;
            toggle_display_mode();
        }
        return;
    }

    handle_key_down_base(keysym);

    if (keysym->sym == key_config[12]) keys[VIEW1] = true;
    if (keysym->sym == key_config[13]) keys[VIEW2] = true;
    if (keysym->sym == key_config[14]) keys[VIEW3] = true;
}

void Input::handle_key_up(SDL_Keysym* keysym)
{
    if (keysym && keysym->sym == display_toggle_key)
    {
        display_toggle_key = SDLK_UNKNOWN;
        return;
    }

    handle_key_up_base(keysym);

    if (keysym->sym == key_config[12]) keys[VIEW1] = false;
    if (keysym->sym == key_config[13]) keys[VIEW2] = false;
    if (keysym->sym == key_config[14]) keys[VIEW3] = false;
}

void Input::handle_joy_axis(SDL_JoyAxisEvent* evt)
{
    const bool controller_side =
        SDL_GameControllerFromInstanceID(evt->which) != nullptr;

    // A recognized GameController also emits raw joystick events. Keep those
    // events available to explicit WHEEL bindings, but do not run the legacy
    // joystick mapping a second time for the same physical device.
    if (!controller_side)
    {
        const int saved_steer = axis[0];
        const int saved_accel = axis[1];
        const int saved_brake = axis[2];

        if (group_has_axis_target(device_binding_t::TARGET_STEER, BINDING_WHEEL))
            axis[0] = -1;
        if (group_has_axis_target(device_binding_t::TARGET_ACCEL, BINDING_WHEEL))
            axis[1] = -1;
        if (group_has_axis_target(device_binding_t::TARGET_BRAKE, BINDING_WHEEL))
            axis[2] = -1;

        handle_axis(evt->which, evt->axis, evt->value);

        axis[0] = saved_steer;
        axis[1] = saved_accel;
        axis[2] = saved_brake;
    }

    capture_raw_axis_motion(evt->which, evt->axis, evt->value);
    apply_device_axis(
        evt->which,
        evt->axis,
        evt->value,
        BINDING_WHEEL);
}

void Input::handle_controller_axis(SDL_ControllerAxisEvent* evt)
{
    // Once either matrix column owns a target, the old single-device axis slots
    // must not overwrite it through the standardized controller path.
    const int saved_steer = axis[0];
    const int saved_accel = axis[1];
    const int saved_brake = axis[2];

    if (any_matrix_axis_target(device_binding_t::TARGET_STEER))
        axis[0] = -1;
    if (any_matrix_axis_target(device_binding_t::TARGET_ACCEL))
        axis[1] = -1;
    if (any_matrix_axis_target(device_binding_t::TARGET_BRAKE))
        axis[2] = -1;

    // While the editor listens to WHEEL, preserve its raw-axis capture state so
    // the duplicate SDL_CONTROLLER event cannot replace the raw axis number.
    const int saved_axis_last = axis_last;
    const int saved_axis_counter = axis_counter;
    const int saved_axis_config = axis_config;
    const SDL_JoystickID saved_axis_last_device = axis_last_device;
    const SDL_JoystickID saved_axis_config_device = axis_config_device;

    handle_axis(evt->which, evt->axis, evt->value);

    if (capture_group == BINDING_WHEEL)
    {
        axis_last = saved_axis_last;
        axis_counter = saved_axis_counter;
        axis_config = saved_axis_config;
        axis_last_device = saved_axis_last_device;
        axis_config_device = saved_axis_config_device;
    }

    axis[0] = saved_steer;
    axis[1] = saved_accel;
    axis[2] = saved_brake;

    apply_device_axis(
        evt->which,
        evt->axis,
        evt->value,
        BINDING_GAMEPAD);
}

void Input::handle_joy_down(SDL_JoyButtonEvent* evt)
{
    const bool controller_side =
        SDL_GameControllerFromInstanceID(evt->which) != nullptr;

    // During WHEEL capture the raw numbering is authoritative. Outside that
    // case an opened GameController leaves capture/navigation to its standardized
    // controller event so duplicate button numbers cannot leak across columns.
    if (!controller_side || capture_group == BINDING_WHEEL)
    {
        joy_button = evt->button;
        joy_button_device = evt->which;
    }

    if (!controller_side)
        handle_joy(evt->which, evt->button, true);

    apply_device_button(
        evt->which,
        evt->button,
        true,
        BINDING_WHEEL);

    if (!controller_side)
    {
        auto matches = [&](int slot)
        {
            return evt->button == pad_config[slot] &&
                   (button_device[slot] == -1 || button_device[slot] == evt->which);
        };

        if (matches(15)) keys[VIEW1] = true;
        if (matches(16)) keys[VIEW2] = true;
        if (matches(17)) keys[VIEW3] = true;
    }
}

void Input::handle_joy_up(SDL_JoyButtonEvent* evt)
{
    const bool controller_side =
        SDL_GameControllerFromInstanceID(evt->which) != nullptr;

    if ((!controller_side || capture_group == BINDING_WHEEL) &&
        joy_button == evt->button &&
        joy_button_device == evt->which)
    {
        joy_button = -1;
        joy_button_device = -1;
    }

    if (!controller_side)
        handle_joy(evt->which, evt->button, false);

    apply_device_button(
        evt->which,
        evt->button,
        false,
        BINDING_WHEEL);

    if (!controller_side)
    {
        auto matches = [&](int slot)
        {
            return evt->button == pad_config[slot] &&
                   (button_device[slot] == -1 || button_device[slot] == evt->which);
        };

        if (matches(15)) keys[VIEW1] = false;
        if (matches(16)) keys[VIEW2] = false;
        if (matches(17)) keys[VIEW3] = false;
    }
}

void Input::handle_joy_hat(SDL_JoyHatEvent* evt)
{
    const bool controller_side =
        SDL_GameControllerFromInstanceID(evt->which) != nullptr;

    if (!controller_side)
    {
        handle_joy_hat_base(evt);
    }
    else if (capture_group == BINDING_WHEEL)
    {
        const Uint8 value = evt->value;
        if (value == SDL_HAT_UP ||
            value == SDL_HAT_DOWN ||
            value == SDL_HAT_LEFT ||
            value == SDL_HAT_RIGHT)
        {
            joy_hat = evt->hat;
            joy_hat_value = value;
            joy_hat_device = evt->which;
        }
        else if (value == SDL_HAT_CENTERED &&
                 joy_hat == evt->hat &&
                 joy_hat_device == evt->which)
        {
            joy_hat_value = SDL_HAT_CENTERED;
        }
    }

    apply_device_hat(
        evt->which,
        evt->hat,
        evt->value,
        BINDING_WHEEL);
}

void Input::handle_controller_down(SDL_ControllerButtonEvent* evt)
{
    const int16_t saved_button = joy_button;
    const SDL_JoystickID saved_button_device = joy_button_device;

    handle_controller_down_base(evt);

    // When WHEEL is being captured the raw joystick button number must survive
    // the duplicate standardized controller event.
    if (capture_group == BINDING_WHEEL)
    {
        joy_button = saved_button;
        joy_button_device = saved_button_device;
    }

    // The D-pad is a permanent menu/navigation fallback, just like the arrow
    // keys. This must not depend on old padconfig values being present.
    switch (evt->button)
    {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            keys[UP] = true;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            keys[DOWN] = true;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            keys[LEFT] = true;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            keys[RIGHT] = true;
            break;
        default:
            break;
    }

    apply_device_button(
        evt->which,
        evt->button,
        true,
        BINDING_GAMEPAD);

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
    const int16_t saved_button = joy_button;
    const SDL_JoystickID saved_button_device = joy_button_device;

    handle_controller_up_base(evt);

    if (capture_group == BINDING_WHEEL)
    {
        joy_button = saved_button;
        joy_button_device = saved_button_device;
    }

    switch (evt->button)
    {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            keys[UP] = false;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            keys[DOWN] = false;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            keys[LEFT] = false;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            keys[RIGHT] = false;
            break;
        default:
            break;
    }

    apply_device_button(
        evt->which,
        evt->button,
        false,
        BINDING_GAMEPAD);

    auto matches = [&](int slot)
    {
        return evt->button == pad_config[slot] &&
               (button_device[slot] == -1 || button_device[slot] == evt->which);
    };

    if (matches(15)) keys[VIEW1] = false;
    if (matches(16)) keys[VIEW2] = false;
    if (matches(17)) keys[VIEW3] = false;
}

void Input::set_rumble(bool enable, float strength, int mode)
{
    // Gamepad rumble is a completely separate output path from steering-wheel
    // force feedback. Never route this through forcefeedback::set(), because
    // that backend belongs to the wheel and previously swallowed pad rumble.

    // Drop a stale controller handle after hot-unplug.
    if (controller && !SDL_GameControllerGetAttached(controller))
    {
        SDL_GameControllerClose(controller);
        controller = nullptr;
        rumble_supported = false;
    }

    // Lazily open the first SDL GameController with working rumble support.
    // This remains as a fallback for unusual hotplug/startup paths; the normal
    // device scan now opens the controller independently of rumble support.
    if (!controller)
    {
        const int count = SDL_NumJoysticks();

        for (int i = 0; i < count; i++)
        {
            if (!SDL_IsGameController(i))
                continue;

            SDL_GameController* candidate = SDL_GameControllerOpen(i);
            if (!candidate)
                continue;

            controller = candidate;
            gamepad = true;
            rumble_supported =
                SDL_GameControllerRumble(controller, 0, 0, 0) == 0;

            const char* name = SDL_GameControllerName(controller);
            std::cout
                << "GameController input enabled: "
                << (name ? name : "Unknown GameController")
                << (rumble_supported ? " with rumble" : " without rumble")
                << std::endl;
            break;
        }
    }

    if (!controller || !rumble_supported)
    {
#ifndef WIN32
        // Preserve the legacy Linux-specific hidraw/evdev fallback. Windows is
        // intentionally excluded: its DirectInput backend is wheel FFB, not
        // gamepad rumble.
        set_rumble_base(enable, strength, mode);
#endif
        return;
    }

    if (!gamepad_rumble::enabled || strength <= 0.0f)
    {
        SDL_GameControllerRumble(controller, 0, 0, 0);
        return;
    }

    // During the game the contextual mixer is authoritative. In particular,
    // do not let the old D_MOTOR-derived 'enable' argument gate pad effects:
    // D_MOTOR changes with the wheel/cabinet FFB mode and caused off-road
    // rumble to disappear whenever FFB was enabled.
    if (cannonball::state == cannonball::STATE_GAME)
    {
        if (gamepad_rumble::dispatch(controller, 0, 0, 45) != 0)
        {
            SDL_GameControllerClose(controller);
            controller = nullptr;
            rumble_supported = false;
        }
        return;
    }

    if (!enable)
    {
        SDL_GameControllerRumble(controller, 0, 0, 0);
        return;
    }

    if (strength > 1.0f)
        strength = 1.0f;

    const Uint16 intensity =
        static_cast<Uint16>(strength * 65535.0f);

    // Retain the legacy non-game behaviour for compatibility/probing.
    const Uint16 low_frequency = mode == 1 ? 0 : intensity;
    const Uint16 high_frequency = intensity;

    if (SDL_GameControllerRumble(
            controller,
            low_frequency,
            high_frequency,
            40) != 0)
    {
        // Treat a failed request as a stale/unusable handle. A later call can
        // probe again, which also makes reconnects recover automatically.
        SDL_GameControllerClose(controller);
        controller = nullptr;
        rumble_supported = false;
    }
}