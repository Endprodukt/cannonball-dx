/***************************************************************************
    Force Feedback (aka Haptic) Support

    Linux: keeps the existing evdev fallback.
    Windows test backend: SDL2 Haptics, modelled after Flycast's wheel path.

    The Windows backend deliberately treats wheel FFB separately from gamepad
    rumble. Wheels use SDL_HAPTIC_STEERING_AXIS and signed constant-force
    levels, so the haptic driver/SDL layer owns the device-specific steering
    actuator mapping.
***************************************************************************/

#include "ffeedback.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef __linux__

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace forcefeedback
{
    static int fd = -1;
    static bool g_supported = false;
    static bool g_enabled = true;
    static int g_gain_percent = 100;
    static struct ff_effect effects[6];

    static bool has_bit(const unsigned long* bits, int bit)
    {
        return (bits[bit / (sizeof(unsigned long) * 8)] >>
                (bit % (sizeof(unsigned long) * 8))) & 1UL;
    }

    bool init(int max_force, int min_force, int duration_ms)
    {
        if (fd >= 0)
        {
            g_supported = true;
            return true;
        }

        const char* devpath = "/dev/input/event";
        char device_file_name[64] = {};

        for (int idx = 0; idx < 100; ++idx)
        {
            std::snprintf(device_file_name, sizeof(device_file_name), "%s%d", devpath, idx);
            int tmp = ::open(device_file_name, O_RDWR | O_CLOEXEC);
            if (tmp < 0)
                continue;

            const size_t bits_per_long = sizeof(unsigned long) * 8;
            unsigned long ev_bits[(EV_MAX + bits_per_long - 1) / bits_per_long] = {};
            if (ioctl(tmp, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1 ||
                !has_bit(ev_bits, EV_FF))
            {
                ::close(tmp);
                continue;
            }

            unsigned long ff_bits[(FF_MAX + bits_per_long - 1) / bits_per_long] = {};
            if (ioctl(tmp, EVIOCGBIT(EV_FF, sizeof(ff_bits)), ff_bits) == -1)
            {
                ::close(tmp);
                continue;
            }

            if (!has_bit(ff_bits, FF_RUMBLE) && !has_bit(ff_bits, FF_PERIODIC))
            {
                ::close(tmp);
                continue;
            }

            fd = tmp;
            break;
        }

        if (fd < 0)
        {
            g_supported = false;
            return false;
        }

        struct input_event gain{};
        gain.type = EV_FF;
        gain.code = FF_GAIN;
        gain.value = 0x7fff;
        (void)write(fd, &gain, sizeof(gain));

        for (int j = 1; j <= 5; ++j)
        {
            struct ff_effect e{};
            e.type = FF_RUMBLE;
            e.id = -1;
            e.u.rumble.strong_magnitude = static_cast<unsigned short>(
                std::max(0, std::min(0x7fff,
                    max_force - (j - 1) * (max_force - min_force) / 4)));
            e.u.rumble.weak_magnitude = e.u.rumble.strong_magnitude / 2;
            e.replay.length = std::max(10, duration_ms);

            if (ioctl(fd, EVIOCSFF, &e) == -1)
            {
                std::memset(&e, 0, sizeof(e));
                e.type = FF_PERIODIC;
                e.id = -1;
                e.u.periodic.waveform = FF_SINE;
                e.u.periodic.magnitude = static_cast<unsigned short>(
                    std::max(0, std::min(0x7fff,
                        max_force - (j - 1) * (max_force - min_force) / 4)));
                e.u.periodic.period = 50;
                e.replay.length = std::max(10, duration_ms);

                if (ioctl(fd, EVIOCSFF, &e) == -1)
                {
                    ::close(fd);
                    fd = -1;
                    g_supported = false;
                    return false;
                }
            }

            effects[j] = e;
        }

        g_supported = true;
        return true;
    }

    int set(int, int force)
    {
        if (!g_supported || !g_enabled || fd < 0)
            return -1;

        int idx = std::max(1, std::min(5, force));
        struct input_event play{};
        play.type = EV_FF;
        play.code = effects[idx].id;
        play.value = 1;
        return write(fd, &play, sizeof(play)) == -1 ? -1 : 0;
    }

    void set_centering_strength(int) {}
    void set_tyre_slip(bool) {}

    void set_enabled(bool enabled)
    {
        g_enabled = enabled;
        if (!enabled)
            stop();
    }

    void set_gain(int percent)
    {
        g_gain_percent = std::max(1, std::min(100, percent));
        (void)g_gain_percent;
    }

    void stop()
    {
        if (fd < 0)
            return;

        for (int j = 1; j <= 5; ++j)
        {
            if (effects[j].id < 0)
                continue;

            struct input_event stop_event{};
            stop_event.type = EV_FF;
            stop_event.code = effects[j].id;
            stop_event.value = 0;
            (void)write(fd, &stop_event, sizeof(stop_event));
        }
    }

    void close()
    {
        stop();

        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }

        g_supported = false;
    }

    bool is_supported()
    {
        return g_supported;
    }
}

#elif defined(_WIN32)

#include <SDL.h>

namespace forcefeedback
{
    static SDL_Joystick* g_joystick = nullptr;
    static SDL_Haptic* g_haptic = nullptr;

    static int g_constant_effect = -1;
    static int g_spring_effect = -1;
    static int g_tyre_slip_effect = -1;

    static bool g_supported = false;
    static bool g_init_attempted = false;
    static bool g_enabled = true;
    static bool g_is_wheel = false;
    static bool g_tyre_slip_active = false;
    static bool g_logged_constant_update_error = false;
    static bool g_logged_constant_run_error = false;

    static unsigned int g_haptic_caps = 0;
    static int g_max_force = 0x7fff;
    static int g_min_force = 0x2fff;
    static int g_gain_percent = 100;
    static int g_centering_percent = 30;

    static int clamp_percent(int percent)
    {
        return std::max(0, std::min(100, percent));
    }

    static bool read_target_vidpid(Uint16& target_vid, Uint16& target_pid)
    {
        target_vid = 0;
        target_pid = 0;

        const char* env = std::getenv("FF_TARGET_VIDPID");
        if (!env || !*env)
            return false;

        unsigned vid = 0;
        unsigned pid = 0;
        if (std::sscanf(env, "%x:%x", &vid, &pid) != 2)
            return false;

        target_vid = static_cast<Uint16>(vid);
        target_pid = static_cast<Uint16>(pid);
        return target_vid != 0 || target_pid != 0;
    }

    static bool candidate_matches_target(SDL_Joystick* joystick, Uint16 target_vid, Uint16 target_pid)
    {
        if (!joystick)
            return false;

        const Uint16 vid = SDL_JoystickGetVendor(joystick);
        const Uint16 pid = SDL_JoystickGetProduct(joystick);

        return (!target_vid || vid == target_vid) &&
               (!target_pid || pid == target_pid);
    }

    static bool select_haptic_device()
    {
        Uint16 target_vid = 0;
        Uint16 target_pid = 0;
        const bool have_target = read_target_vidpid(target_vid, target_pid);

        // Pass order is intentional:
        //  1. requested VID:PID if it is actually a wheel
        //  2. any SDL-classified wheel
        //  3. requested VID:PID even if SDL did not classify it as a wheel
        //  4. any constant-force capable haptic joystick
        // This prevents a rumble-capable gamepad selected as pad_id from stealing
        // wheel FFB in a multi-device setup.
        for (int pass = 0; pass < 4; ++pass)
        {
            const bool require_target = pass == 0 || pass == 2;
            const bool require_wheel = pass == 0 || pass == 1;

            if (require_target && !have_target)
                continue;

            const int count = SDL_NumJoysticks();
            for (int index = 0; index < count; ++index)
            {
                SDL_Joystick* joystick = SDL_JoystickOpen(index);
                if (!joystick)
                    continue;

                const bool is_wheel =
                    SDL_JoystickGetType(joystick) == SDL_JOYSTICK_TYPE_WHEEL;

                if ((require_wheel && !is_wheel) ||
                    (require_target && !candidate_matches_target(joystick, target_vid, target_pid)))
                {
                    SDL_JoystickClose(joystick);
                    continue;
                }

                SDL_Haptic* haptic = SDL_HapticOpenFromJoystick(joystick);
                if (!haptic)
                {
                    SDL_JoystickClose(joystick);
                    continue;
                }

                const unsigned int caps = SDL_HapticQuery(haptic);
                if ((caps & SDL_HAPTIC_CONSTANT) == 0)
                {
                    SDL_HapticClose(haptic);
                    SDL_JoystickClose(joystick);
                    continue;
                }

                g_joystick = joystick;
                g_haptic = haptic;
                g_haptic_caps = caps;
                g_is_wheel = is_wheel;

                const char* name = SDL_JoystickName(joystick);
                std::cout
                    << "SDL FFB: selected " << (name ? name : "Unknown device")
                    << " | VID:PID 0x" << std::hex
                    << static_cast<unsigned>(SDL_JoystickGetVendor(joystick))
                    << ":0x" << static_cast<unsigned>(SDL_JoystickGetProduct(joystick))
                    << std::dec
                    << " | wheel=" << (g_is_wheel ? "yes" : "no")
                    << " | caps=0x" << std::hex << g_haptic_caps << std::dec
                    << " | effects=" << SDL_HapticNumEffects(g_haptic)
                    << " | playing=" << SDL_HapticNumEffectsPlaying(g_haptic)
                    << std::endl;

                return true;
            }
        }

        return false;
    }

    static SDL_HapticDirection steering_direction()
    {
        SDL_HapticDirection direction{};
        direction.type = g_is_wheel ? SDL_HAPTIC_STEERING_AXIS : SDL_HAPTIC_CARTESIAN;
        direction.dir[0] = 1;
        return direction;
    }

    static bool create_constant_effect()
    {
        SDL_HapticEffect effect{};
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction = steering_direction();
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.delay = 0;
        effect.constant.level = 0;

        g_constant_effect = SDL_HapticNewEffect(g_haptic, &effect);
        if (g_constant_effect < 0)
        {
            std::cout << "SDL FFB: constant-force effect unavailable: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        std::cout << "SDL FFB: constant-force effect id="
                  << g_constant_effect << std::endl;
        return true;
    }

    static int effective_centering_percent()
    {
        return g_tyre_slip_active
            ? (g_centering_percent * 80 + 50) / 100
            : g_centering_percent;
    }

    static SDL_HapticEffect make_spring_effect(int percent)
    {
        SDL_HapticEffect effect{};
        effect.type = SDL_HAPTIC_SPRING;
        effect.condition.direction = steering_direction();
        effect.condition.length = SDL_HAPTIC_INFINITY;

        const int strength = (0x7fff * clamp_percent(percent)) / 100;
        effect.condition.left_sat[0] = 0xffff;
        effect.condition.right_sat[0] = 0xffff;
        effect.condition.left_coeff[0] = static_cast<Sint16>(strength);
        effect.condition.right_coeff[0] = static_cast<Sint16>(strength);
        effect.condition.deadband[0] = 0;
        effect.condition.center[0] = 0;
        return effect;
    }

    static bool create_spring_effect()
    {
        if ((g_haptic_caps & SDL_HAPTIC_SPRING) == 0)
            return false;

        SDL_HapticEffect effect = make_spring_effect(effective_centering_percent());
        g_spring_effect = SDL_HapticNewEffect(g_haptic, &effect);
        if (g_spring_effect < 0)
        {
            std::cout << "SDL FFB: spring effect unavailable: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        // Match Flycast's lifecycle: create the spring here, but do not start
        // it until CannonBall explicitly applies its configured centering load.
        return true;
    }

    static bool ensure_initialized()
    {
        if (g_supported)
            return true;

        if (g_init_attempted)
            return false;

        return init(0x7fff, 0x2fff, 50);
    }

    bool init(int max_force, int min_force, int)
    {
        if (g_supported)
            return true;

        g_init_attempted = true;
        g_max_force = std::max(0, std::min(0x7fff, max_force));
        g_min_force = std::max(0, std::min(g_max_force, min_force));

        if (!select_haptic_device())
        {
            std::cout << "SDL FFB: no constant-force capable wheel found" << std::endl;
            return false;
        }

        if (g_haptic_caps & SDL_HAPTIC_AUTOCENTER)
            SDL_HapticSetAutocenter(g_haptic, 0);

        if (g_haptic_caps & SDL_HAPTIC_GAIN)
            SDL_HapticSetGain(g_haptic, 100);

        if (!create_constant_effect())
        {
            close();
            g_init_attempted = true;
            return false;
        }

        create_spring_effect();

        g_supported = true;
        std::cout << "SDL FFB: steering-axis backend enabled" << std::endl;
        return true;
    }

    int set(int xdirection, int force)
    {
        if (!ensure_initialized() || !g_enabled || g_constant_effect < 0)
            return -1;

        force = std::max(0, std::min(7, force));

        int magnitude =
            g_max_force - (((g_max_force - g_min_force) * force) / 7);
        magnitude = (magnitude * g_gain_percent) / 100;
        magnitude = std::max(0, std::min(0x7fff, magnitude));

        int sign = 0;
        if (xdirection < 0x08)
            sign = -1;
        else if (xdirection > 0x08)
            sign = 1;

        SDL_HapticEffect effect{};
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.delay = 0;

        if (g_is_wheel)
        {
            // Flycast-style wheel path: fixed steering direction, signed torque.
            effect.constant.direction = steering_direction();
            effect.constant.level = static_cast<Sint16>(sign * magnitude);
        }
        else
        {
            // Generic fallback for devices SDL does not classify as a wheel.
            effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
            effect.constant.direction.dir[0] = sign < 0 ? -1 : 1;
            effect.constant.level = static_cast<Sint16>(sign == 0 ? 0 : magnitude);
        }

        if (SDL_HapticUpdateEffect(g_haptic, g_constant_effect, &effect) < 0)
        {
            if (!g_logged_constant_update_error)
            {
                std::cout << "SDL FFB: constant-force update failed: "
                          << SDL_GetError() << std::endl;
                g_logged_constant_update_error = true;
            }
            return -1;
        }

        g_logged_constant_update_error = false;

        if (sign == 0)
        {
            SDL_HapticStopEffect(g_haptic, g_constant_effect);
            return 0;
        }

        if (SDL_HapticRunEffect(g_haptic, g_constant_effect, 1) < 0)
        {
            if (!g_logged_constant_run_error)
            {
                std::cout << "SDL FFB: constant-force run failed: "
                          << SDL_GetError() << std::endl;
                g_logged_constant_run_error = true;
            }
            return -1;
        }

        g_logged_constant_run_error = false;
        return 0;
    }

    void set_centering_strength(int percent)
    {
        g_centering_percent = clamp_percent(percent);

        if (!ensure_initialized() || !g_enabled || g_spring_effect < 0)
            return;

        const int effective_percent = effective_centering_percent();
        if (effective_percent == 0)
        {
            SDL_HapticStopEffect(g_haptic, g_spring_effect);
            return;
        }

        SDL_HapticEffect effect = make_spring_effect(effective_percent);
        if (SDL_HapticUpdateEffect(g_haptic, g_spring_effect, &effect) < 0)
        {
            std::cout << "SDL FFB: unable to update spring: "
                      << SDL_GetError() << std::endl;
            return;
        }

        if (SDL_HapticRunEffect(g_haptic, g_spring_effect, 1) < 0)
        {
            std::cout << "SDL FFB: unable to run spring: "
                      << SDL_GetError() << std::endl;
        }
    }

    static bool create_tyre_slip_effect()
    {
        if ((g_haptic_caps & SDL_HAPTIC_SINE) == 0)
            return false;

        SDL_HapticEffect effect{};
        effect.type = SDL_HAPTIC_SINE;
        effect.periodic.direction = steering_direction();
        effect.periodic.length = SDL_HAPTIC_INFINITY;
        effect.periodic.period = 45; // ~22 Hz, matching the previous backend
        effect.periodic.magnitude = 0;

        g_tyre_slip_effect = SDL_HapticNewEffect(g_haptic, &effect);
        if (g_tyre_slip_effect < 0)
        {
            std::cout << "SDL FFB: tyre-slip sine unavailable: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        return true;
    }

    void set_tyre_slip(bool active)
    {
        if (!ensure_initialized() || !g_enabled)
            active = false;

        if (active == g_tyre_slip_active)
            return;

        g_tyre_slip_active = active;

        // Preserve the existing DX behaviour: tyre slip lightens centering by 20%.
        set_centering_strength(g_centering_percent);

        if (!active)
        {
            if (g_tyre_slip_effect >= 0)
                SDL_HapticStopEffect(g_haptic, g_tyre_slip_effect);
            return;
        }

        if (g_tyre_slip_effect < 0 && !create_tyre_slip_effect())
            return;

        const int magnitude =
            std::max(0, std::min(0x7fff,
                (0x7fff * 15 * g_gain_percent) / 10000));

        SDL_HapticEffect effect{};
        effect.type = SDL_HAPTIC_SINE;
        effect.periodic.direction = steering_direction();
        effect.periodic.length = SDL_HAPTIC_INFINITY;
        effect.periodic.period = 45;
        effect.periodic.magnitude = static_cast<Sint16>(magnitude);

        if (SDL_HapticUpdateEffect(g_haptic, g_tyre_slip_effect, &effect) == 0)
            SDL_HapticRunEffect(g_haptic, g_tyre_slip_effect, 1);
    }

    void set_enabled(bool enabled)
    {
        g_enabled = enabled;

        if (!g_supported || !g_haptic)
            return;

        if (!enabled)
        {
            if (g_constant_effect >= 0)
                SDL_HapticStopEffect(g_haptic, g_constant_effect);
            if (g_spring_effect >= 0)
                SDL_HapticStopEffect(g_haptic, g_spring_effect);
            if (g_tyre_slip_effect >= 0)
                SDL_HapticStopEffect(g_haptic, g_tyre_slip_effect);
            g_tyre_slip_active = false;
        }
        else
        {
            set_centering_strength(g_centering_percent);
        }
    }

    void set_gain(int percent)
    {
        g_gain_percent = std::max(1, std::min(100, percent));
    }

    void stop()
    {
        if (g_haptic && g_constant_effect >= 0)
            SDL_HapticStopEffect(g_haptic, g_constant_effect);
    }

    void close()
    {
        if (g_haptic)
        {
            if (g_tyre_slip_effect >= 0)
            {
                SDL_HapticStopEffect(g_haptic, g_tyre_slip_effect);
                SDL_HapticDestroyEffect(g_haptic, g_tyre_slip_effect);
            }

            if (g_spring_effect >= 0)
            {
                SDL_HapticStopEffect(g_haptic, g_spring_effect);
                SDL_HapticDestroyEffect(g_haptic, g_spring_effect);
            }

            if (g_constant_effect >= 0)
            {
                SDL_HapticStopEffect(g_haptic, g_constant_effect);
                SDL_HapticDestroyEffect(g_haptic, g_constant_effect);
            }

            SDL_HapticClose(g_haptic);
        }

        if (g_joystick)
            SDL_JoystickClose(g_joystick);

        g_joystick = nullptr;
        g_haptic = nullptr;
        g_constant_effect = -1;
        g_spring_effect = -1;
        g_tyre_slip_effect = -1;
        g_haptic_caps = 0;
        g_supported = false;
        g_init_attempted = false;
        g_is_wheel = false;
        g_tyre_slip_active = false;
        g_logged_constant_update_error = false;
        g_logged_constant_run_error = false;
    }

    bool is_supported()
    {
        // The legacy input code only called the old DirectInput backend when
        // SDL rumble was unavailable. For this test backend, probe lazily so a
        // rumble-capable gamepad can coexist with a separate FFB wheel.
        return g_supported || ensure_initialized();
    }
}

#else

namespace forcefeedback
{
    bool init(int, int, int) { return false; }
    int set(int, int) { return -1; }
    void close() {}
    bool is_supported() { return false; }
    void set_centering_strength(int) {}
    void set_tyre_slip(bool) {}
    void set_enabled(bool) {}
    void set_gain(int) {}
    void stop() {}
}

#endif
