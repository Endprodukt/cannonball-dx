/***************************************************************************
    SDL Based Input Handling.

    Populates keys array with user input.
    If porting to a non-SDL platform, you would need to replace this class.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <fstream>
#include <mutex>

namespace input_diagnostic
{
    inline std::ofstream log_file;
    inline std::mutex log_mutex;
    inline bool started = false;
    inline Uint32 start_ticks = 0;

    inline const char* safe_name(SDL_JoystickID instance_id)
    {
        SDL_Joystick* joystick = SDL_JoystickFromInstanceID(instance_id);
        if (!joystick)
            return "Unknown SDL Device";

        const char* name = SDL_JoystickName(joystick);
        return name ? name : "Unknown SDL Device";
    }

    inline int event_watch(void*, SDL_Event* event)
    {
        if (!event)
            return 1;

        std::lock_guard<std::mutex> guard(log_mutex);
        if (!log_file.is_open())
            return 1;

        const Uint32 elapsed = SDL_GetTicks() - start_ticks;

        if (event->type == SDL_JOYAXISMOTION)
        {
            SDL_Joystick* joystick =
                SDL_JoystickFromInstanceID(event->jaxis.which);

            log_file
                << "[" << elapsed << " ms] JOYAXIS"
                << " device=" << event->jaxis.which
                << " name=\"" << safe_name(event->jaxis.which) << "\""
                << " axis=" << static_cast<int>(event->jaxis.axis)
                << " value=" << event->jaxis.value
                << " axes=" << (joystick ? SDL_JoystickNumAxes(joystick) : -1)
                << " gamecontroller="
                << (SDL_GameControllerFromInstanceID(event->jaxis.which) ? "yes" : "no")
                << '\n';
            log_file.flush();
        }
        else if (event->type == SDL_CONTROLLERAXISMOTION)
        {
            log_file
                << "[" << elapsed << " ms] CONTROLLERAXIS"
                << " device=" << event->caxis.which
                << " name=\"" << safe_name(event->caxis.which) << "\""
                << " axis=" << static_cast<int>(event->caxis.axis)
                << " value=" << event->caxis.value
                << '\n';
            log_file.flush();
        }

        return 1;
    }

    inline void ensure_started()
    {
        std::lock_guard<std::mutex> guard(log_mutex);
        if (started)
            return;

        started = true;
        start_ticks = SDL_GetTicks();

        char* base_path = SDL_GetBasePath();
        std::string path = base_path ? std::string(base_path) : std::string();
        if (base_path)
            SDL_free(base_path);

        path += "cannonball_input_diagnostic.txt";
        log_file.open(path, std::ios::out | std::ios::trunc);
        if (!log_file.is_open())
            return;

        SDL_version linked_version{};
        SDL_GetVersion(&linked_version);

        log_file
            << "CannonBall DX SDL input diagnostic\n"
            << "SDL version: "
            << static_cast<int>(linked_version.major) << '.'
            << static_cast<int>(linked_version.minor) << '.'
            << static_cast<int>(linked_version.patch) << "\n"
            << "Log path: " << path << "\n"
            << "Purpose: diagnose vJoy/BackForceFeeder brake-axis detection\n\n"
            << "=== SDL DEVICE INVENTORY ===\n";

        const int count = SDL_NumJoysticks();
        log_file << "Devices found: " << count << '\n';

        for (int i = 0; i < count; i++)
        {
            char guid_string[33] = {};
            SDL_JoystickGetGUIDString(
                SDL_JoystickGetDeviceGUID(i),
                guid_string,
                sizeof(guid_string));

            const char* name = SDL_JoystickNameForIndex(i);
            const SDL_JoystickID instance_id =
                SDL_JoystickGetDeviceInstanceID(i);

            log_file
                << "device_index=" << i
                << " instance=" << instance_id
                << " name=\"" << (name ? name : "Unknown SDL Device") << "\""
                << " gamecontroller=" << (SDL_IsGameController(i) ? "yes" : "no")
                << " guid=" << guid_string
                << '\n';
        }

        log_file
            << "============================\n\n"
            << "Move steering, accelerator and brake through their full travel.\n"
            << "Then try binding the brake in CannonBall DX and close the game.\n\n"
            << "=== AXIS EVENTS ===\n";
        log_file.flush();

        SDL_AddEventWatch(event_watch, nullptr);
    }
}

struct InputDevice
{
    InputDevice()
    {
        // The first InputDevice is created after SDL has opened a joystick,
        // which makes this a safe point to attach the diagnostic event watch.
        input_diagnostic::ensure_started();
    }

    SDL_Joystick* joystick = nullptr;
    SDL_JoystickID instance_id = -1;
    SDL_JoystickGUID guid{};

    std::string guid_string;
    std::string name;

    int axes = 0;
    int buttons = 0;
    int hats = 0;
};

class Input
{
public:

    SDL_JoystickID joy_button_device;

    int joy_hat = -1;
    Uint8 joy_hat_value = SDL_HAT_CENTERED;
    SDL_JoystickID joy_hat_device = -1;

    void set_axis_binding(int slot, int axis, SDL_JoystickID device);
    void set_button_binding(int slot, int button, SDL_JoystickID device);
    void set_hat_binding(int slot, int hat, int value, SDL_JoystickID device);

    // The editor has three logical input groups. KEYBOARD is handled by the
    // existing keyconfig array. GAMEPAD uses the primary SDL GameController.
    // WHEEL is an aggregate bucket for raw non-gamepad SDL joystick devices
    // such as wheels, pedals, shifters, button boxes and joysticks.
    enum binding_groups
    {
        BINDING_GAMEPAD = 0,
        BINDING_WHEEL = 1,
    };

    const std::vector<InputDevice>& get_devices() const;
    std::string get_device_signature(SDL_JoystickID device) const;
    SDL_JoystickID get_gamepad_device() const;

    // Convert bindings created by the first matrix implementation into the
    // new logical GAMEPAD/WHEEL groups when the editor is opened.
    void normalize_device_bindings();

    void set_device_binding(
        int target,
        int type,
        int index,
        int value,
        SDL_JoystickID device,
        int group);

    // Clear all assignments for one action in one logical group. WHEEL may
    // contain several physical devices and several controls per action.
    void clear_device_bindings(int target, int group);

    // Retained for compatibility with older callers.
    void clear_device_binding(int target, SDL_JoystickID device);

    void scan_joysticks();
    void add_joystick(int device_index);
    void remove_joystick(SDL_JoystickID instance_id);

    void restore_axis_bindings();
    void sync_bound_axes();

    const InputDevice* find_device(SDL_JoystickID instance_id) const;

    enum presses
    {
        LEFT  = 0,
        RIGHT = 1,
        UP    = 2,
        DOWN  = 3,
        ACCEL = 4,
        BRAKE = 5,
        GEAR1 = 6,
        GEAR2 = 7,

        START = 8,
        COIN  = 9,
        VIEWPOINT = 10,

        PAUSE = 11,
        STEP  = 12,
        TIMER = 13,
        MENU = 14,

        // Optional direct camera selection. VIEWPOINT remains the existing
        // cycle-through-views control for single-button cabinets/controllers.
        VIEW1 = 15,
        VIEW2 = 16,
        VIEW3 = 17,
    };

    bool keys[18];
    bool keys_old[18];

    enum limits
    {
        SW_LEFT   = 0,
        SW_CENTRE = 1,
        SW_RIGHT  = 2,
    };
    bool motor_limits[3];

    // Has gamepad been found?
    bool gamepad;

    // Gamepad supports rumble?
    int rumble_supported;

    // Use analog controls
    int analog;

    // Latch last key press for redefines
    int key_press;

    // Latch last joystick button press for redefines
    int16_t joy_button;

    // Analog Controls
    int wheel, a_wheel;
    int a_accel;
    int a_brake;
    int a_motor;

    Input(void);
    ~Input(void);

    void init(int, int*, int*, const int, int*, bool*, int*);
    void open_joy();
    void close_joy();

    void handle_key_up(SDL_Keysym*);
    void handle_key_down(SDL_Keysym*);
    void handle_joy_axis(SDL_JoyAxisEvent*);
    void handle_joy_down(SDL_JoyButtonEvent*);
    void handle_joy_up(SDL_JoyButtonEvent*);
    void handle_joy_hat(SDL_JoyHatEvent*);
    void handle_controller_axis(SDL_ControllerAxisEvent*);
    void handle_controller_down(SDL_ControllerButtonEvent*);
    void handle_controller_up(SDL_ControllerButtonEvent*);
    void frame_done();
    bool is_pressed(presses p);
    bool is_pressed_clear(presses p);
    bool has_pressed(presses p);
    void reset_axis_config();
    int get_axis_config(SDL_JoystickID* device = nullptr);
    void set_rumble(bool, float strength = 1.0f, int mode = 0);

private:
    SDL_JoystickID axis_device[4];
    SDL_JoystickID button_device[18];

    SDL_JoystickID axis_last_device;
    SDL_JoystickID axis_config_device;

    static const int CENTRE = 0x80;

    std::vector<InputDevice> devices;

    // SDL Joystick / Keypad
    SDL_Joystick *stick;
    SDL_GameController* controller;
    SDL_Haptic* haptic;
    int hidraw_device = -1; // used for gamepad haptics where not supported by SDL

    // Configurations for keyboard and joypad
    int pad_id;
    int* pad_config;
    int* key_config;
    int* axis;
    bool* invert;

    int wheel_zone;
    int wheel_dead;

    // Last axis used
    int axis_last , axis_counter, axis_config;

    struct AxisCaptureBaseline
    {
        SDL_JoystickID device = -1;
        int axis = -1;
        int value = 0;
    };

    std::vector<AxisCaptureBaseline> axis_capture_baseline;

    void bind_axis(SDL_GameControllerAxis ax, int offset);
    void bind_button(SDL_GameControllerButton button, int offset);
    void handle_key(const int, const bool);
    void handle_joy(SDL_JoystickID device, const uint8_t button, const bool is_pressed);
    void handle_axis(SDL_JoystickID device, const uint8_t axis, const int16_t value);
    void store_last_axis(SDL_JoystickID device, const uint8_t axis, const int16_t value);
    void capture_raw_axis_motion(SDL_JoystickID device, const uint8_t axis, const int16_t value);
    int scale_trigger(const int);

    void apply_device_button(SDL_JoystickID device, int button, bool is_pressed, int group);
    void apply_device_axis(SDL_JoystickID device, int axis, int value, int group);
    void apply_device_hat(SDL_JoystickID device, int hat, int value, int group);
    void set_device_target(int target, bool is_pressed);
    void ensure_gamecontroller_open();

    // The current multi-device implementations are retained under these names
    // and wrapped by input.cpp to add optional direct-view/per-device bindings.
    void scan_joysticks_base();
    void add_joystick_base(int);
    void remove_joystick_base(SDL_JoystickID);
    void set_button_binding_base(int, int, SDL_JoystickID);
    void handle_key_down_base(SDL_Keysym*);
    void handle_key_up_base(SDL_Keysym*);
    void handle_joy_axis_base(SDL_JoyAxisEvent*);
    void handle_joy_down_base(SDL_JoyButtonEvent*);
    void handle_joy_up_base(SDL_JoyButtonEvent*);
    void handle_joy_hat_base(SDL_JoyHatEvent*);
    void handle_controller_axis_base(SDL_ControllerAxisEvent*);
    void handle_controller_down_base(SDL_ControllerButtonEvent*);
    void handle_controller_up_base(SDL_ControllerButtonEvent*);
    void reset_axis_config_base();
    void set_rumble_base(bool, float, int);
};

extern Input input;