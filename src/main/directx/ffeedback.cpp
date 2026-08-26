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

    int set(int, int force, const std::source_location&)
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

    void set_centering_strength(int, const std::source_location&) {}
    void set_tyre_slip(bool, const std::source_location&) {}

    void set_enabled(bool enabled)
    {
        g_enabled = enabled;
        if (!enabled)
            stop();
    }

    void set_gain(int percent)
    {
        g_gain_percent = std::max(0, std::min(100, percent));
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

#include "main.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinitengine.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"

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
    static bool g_tyre_slip_prestart = false;
    static bool g_logged_constant_update_error = false;
    static bool g_logged_constant_run_error = false;

    static unsigned int g_haptic_caps = 0;
    static int g_gain_percent = 100;
    static int g_centering_percent = 30;
    static int g_applied_centering_percent = -1;
    static int g_tyre_slip_strength_percent = 15;
    static int g_tyre_slip_spring_percent = 67;
    static int g_offroad_pull_direction = 0;
    static bool g_offroad_level_override_active = false;
    static int g_offroad_level_override = 0;

    static int clamp_percent(int percent)
    {
        return std::max(0, std::min(100, percent));
    }

    static bool source_function_contains(
        const std::source_location& source,
        const char* text)
    {
        const char* function_name = source.function_name();
        return function_name && std::strstr(function_name, text) != nullptr;
    }

    static bool source_file_contains(
        const std::source_location& source,
        const char* text)
    {
        const char* file_name = source.file_name();
        return file_name && std::strstr(file_name, text) != nullptr;
    }

    static int effect_setting(const char* name, int default_value)
    {
        return config.ffb_effect_setting(name, default_value);
    }

    static int spring_setting(const char* name, int default_value)
    {
        return config.ffb_spring_setting(name, default_value);
    }

    static int scale_value(int value, int percent, int denominator = 100)
    {
        if (denominator <= 0)
            denominator = 100;

        long long product =
            static_cast<long long>(value) * percent;

        if (product >= 0)
            product += denominator / 2;
        else
            product -= denominator / 2;

        return static_cast<int>(product / denominator);
    }

    static int master_effect_gain(int effect_percent)
    {
        return scale_value(
            clamp_percent(config.controls.ffb_strength),
            clamp_percent(effect_percent));
    }

    static bool low_speed_crash_bump()
    {
        return
            ocrash.crash_counter &&
            !ocrash.is_flip() &&
            ocrash.crash_state == 1 &&
            (oinitengine.car_increment >> 16) == 0;
    }

    static int tuned_gain_for_source(const std::source_location& source)
    {
        // Every named effect uses the same rule:
        // hardware max * in-game master * config effect * instantaneous shape.
        // The config value is therefore the real maximum for that effect when
        // the in-game master is 100.
        if (source_function_contains(source, "apply_surface_rattle"))
            return master_effect_gain(effect_setting("sand", 3));

        if (source_function_contains(source, "set_shift_force"))
            return master_effect_gain(effect_setting("gear_shift", 49));

        if (source_function_contains(source, "set_crash_yank"))
        {
            return master_effect_gain(
                ocrash.is_flip()
                    ? effect_setting("crash_flip", 70)
                    : effect_setting("crash_spin", 70));
        }

        if (source_function_contains(source, "set_start_intro_force"))
        {
            return scale_value(
                master_effect_gain(effect_setting("start_steering", 70)),
                clamp_percent(g_gain_percent));
        }

        if (source_function_contains(source, "apply_music_detent_ffb") ||
            source_function_contains(source, "set_music_detent_force"))
        {
            return master_effect_gain(effect_setting("music_selector", 7));
        }

        if (source_function_contains(source, "do_motors") &&
            ocrash.skid_counter)
        {
            return master_effect_gain(effect_setting("traffic_skid", 70));
        }

        if (source_function_contains(source, "apply_crash_ffb_force") &&
            ocrash.crash_counter)
        {
            if (low_speed_crash_bump())
                return master_effect_gain(effect_setting("crash_bump", 70));

            if (ocrash.is_flip())
            {
                if (ocrash.crash_state >= 5)
                    return master_effect_gain(effect_setting("crash_flip_landing", 70));

                return master_effect_gain(effect_setting("crash_flip_impact", 70));
            }

            return master_effect_gain(effect_setting("crash_spin_impact", 70));
        }

        // Unnamed constant-force paths are governed only by the in-game master.
        return clamp_percent(config.controls.ffb_strength);
    }

    static int normalized_force_for_source(
        int force,
        const std::source_location& source)
    {
        force = std::max(0, std::min(7, force));

        // These effects already have their amplitude expressed by their config
        // percentage and their time/cadence envelope. Their old motor command
        // was only a legacy cabinet-strength choice, so normalize the active
        // pulse to the full configured amplitude.
        if (source_function_contains(source, "apply_surface_rattle") ||
            source_function_contains(source, "set_start_intro_force"))
        {
            return 0;
        }

        if (source_function_contains(source, "set_shift_force"))
        {
            // Keep a small opposite-direction rebound instead of letting the
            // old force=7 collapse to literal zero on the new linear backend.
            return force >= 7 ? 5 : force;
        }

        if (source_function_contains(source, "set_crash_yank"))
        {
            // Medium-speed spin uses force=1 for its sustained yank. That is
            // the peak of this named effect and must reach its config maximum.
            if (!ocrash.is_flip())
                return 0;

            // Flip uses 0/1/2 as a real shape; 0 remains the peak.
            return force;
        }

        if (source_function_contains(source, "apply_crash_ffb_force") &&
            ocrash.crash_counter)
        {
            if (low_speed_crash_bump())
            {
                // Bump uses force=2 for the main impact and force=7 for the
                // rebound. Normalize the impact to 100% and retain a small
                // rebound at about 29% of the configured value.
                return force >= 7 ? 5 : 0;
            }

            if (!ocrash.is_flip())
            {
                // Spin impact uses force=1 as its strongest phase.
                return 0;
            }

            if (ocrash.crash_state >= 5 && force >= 7)
            {
                // Preserve the final post-landing settle as a very small pulse.
                return 6;
            }
        }

        return force;
    }

    static int offroad_speed_percent()
    {
        const int car_inc =
            static_cast<int>(oinitengine.car_increment >> 16);

        if (car_inc <= 0x32)
            return 20;
        if (car_inc <= 0x50)
            return 40;
        if (car_inc <= 0x6E)
            return 70;
        return 100;
    }

    static void tune_offroad_command(
        int& xdirection,
        int& force,
        const std::source_location& source)
    {
        g_offroad_level_override_active = false;
        g_offroad_level_override = 0;

        if (!source_function_contains(source, "motor_output") ||
            outrun.game_state != GS_INGAME ||
            oferrari.wheel_state == OFerrari::WHEELS_ON)
        {
            if (oferrari.wheel_state == OFerrari::WHEELS_ON)
                g_offroad_pull_direction = 0;
            return;
        }

        if (oferrari.wheel_state == OFerrari::WHEELS_LEFT_OFF)
            g_offroad_pull_direction = 1;
        else if (oferrari.wheel_state == OFerrari::WHEELS_RIGHT_OFF)
            g_offroad_pull_direction = -1;
        else if (oferrari.wheel_state == OFerrari::WHEELS_OFF &&
                 g_offroad_pull_direction == 0)
            g_offroad_pull_direction = oinitengine.car_x_pos >= 0 ? -1 : 1;

        const bool fully_offroad =
            oferrari.wheel_state == OFerrari::WHEELS_OFF;

        const int combined_force = xdirection - 0x08;
        const int current_pull_magnitude = fully_offroad ? 2 : 3;
        const int current_pull =
            current_pull_magnitude * g_offroad_pull_direction;
        const int current_rumble =
            combined_force - current_pull;

        const int configured_rumble =
            fully_offroad
                ? effect_setting("offroad_rumble_full", 10)
                : effect_setting("offroad_rumble_one_wheel", 10);

        // The legacy table reaches +/-6 at its fastest cadence. Normalize that
        // peak to 100% so config=100 can actually reach the full configured
        // rumble at high speed, while the speed envelope still makes low-speed
        // shoulder contact deliberately softer.
        const int speed_percent = offroad_speed_percent();
        int rumble_level =
            scale_value(0x7fff, current_rumble, 6);
        rumble_level =
            scale_value(rumble_level, configured_rumble);
        rumble_level =
            scale_value(rumble_level, speed_percent);

        const int pull_percent =
            fully_offroad
                ? effect_setting("offroad_pull_full", 21)
                : effect_setting("offroad_pull_one_wheel", 10);

        // Pull is high-resolution too: 100 means the full constant-force range,
        // 1 means one percent, with no seven-step quantisation.
        const int pull_level =
            scale_value(
                0x7fff * g_offroad_pull_direction,
                pull_percent);

        g_offroad_level_override =
            std::max(-0x7fff,
                std::min(0x7fff, rumble_level + pull_level));
        g_offroad_level_override_active = true;

        force = std::max(0, std::min(7, force));
    }

    static int configured_low_speed_spring()
    {
        return scale_value(
            config.controls.centering_strength,
            spring_setting("low_speed", 40));
    }

    static int configured_high_speed_spring()
    {
        return scale_value(
            config.controls.centering_strength,
            spring_setting("high_speed", 100));
    }

    static int tuned_centering_for_source(
        int requested_percent,
        const std::source_location& source)
    {
        if (source_file_contains(source, "menu.cpp") ||
            source_file_contains(source, "menu_base.cpp") ||
            source_function_contains(source, "reset_music_detent_ffb"))
        {
            return configured_low_speed_spring();
        }

        if (source_function_contains(source, "apply_music_detent_ffb"))
        {
            return requested_percent;
        }

        if (!source_file_contains(source, "ooutputs_base.cpp"))
            return requested_percent;

        if (source_function_contains(source, "init"))
            return configured_low_speed_spring();

        if (!source_function_contains(source, "update_centering_strength") &&
            !source_function_contains(source, "tick"))
        {
            return requested_percent;
        }

        const int low_speed_strength = configured_low_speed_spring();
        const int high_speed_strength = configured_high_speed_spring();
        int target_strength = low_speed_strength;

        if (outrun.game_state == GS_INGAME &&
            !ocrash.crash_counter &&
            !ocrash.skid_counter &&
            oferrari.wheel_state == OFerrari::WHEELS_ON)
        {
            const int car_inc =
                static_cast<int>(oinitengine.car_increment >> 16);
            const int speed_start = spring_setting("speed_start", 100);
            int speed_full = spring_setting("speed_full", 240);

            if (speed_full <= speed_start)
                speed_full = speed_start + 1;

            int speed_factor = car_inc - speed_start;
            const int speed_span = speed_full - speed_start;

            if (speed_factor < 0)
                speed_factor = 0;
            else if (speed_factor > speed_span)
                speed_factor = speed_span;

            target_strength =
                low_speed_strength +
                scale_value(
                    high_speed_strength - low_speed_strength,
                    speed_factor,
                    speed_span);
        }

        if (outrun.game_state == GS_INGAME)
        {
            int spring_percent = 100;

            if (ocrash.crash_counter)
            {
                if (low_speed_crash_bump())
                {
                    spring_percent = spring_setting("crash_bump", 65);
                }
                else if (!ocrash.is_flip())
                {
                    spring_percent =
                        ocrash.crash_state <= 4
                            ? spring_setting("crash_spin", 35)
                            : spring_setting("crash_recovery", 70);
                }
                else if (ocrash.crash_state <= 1)
                {
                    spring_percent = spring_setting("crash_flip_start", 45);
                }
                else if (ocrash.crash_state == 2)
                {
                    spring_percent = spring_setting("crash_flip_airborne", 10);
                }
                else if (ocrash.crash_state <= 4)
                {
                    spring_percent = spring_setting("crash_flip_transition", 25);
                }
                else if (ocrash.crash_state == 5)
                {
                    spring_percent = spring_setting("crash_flip_landing", 45);
                }
                else
                {
                    spring_percent = spring_setting("crash_flip_recovery", 70);
                }
            }
            else if (ocrash.skid_counter)
            {
                spring_percent = spring_setting("traffic_skid", 50);
            }

            if (ocrash.crash_counter || ocrash.skid_counter)
            {
                target_strength = scale_value(
                    config.controls.centering_strength,
                    spring_percent);
            }
        }

        return target_strength;
    }

    static int constant_force_level(int force, int gain_percent)
    {
        force = std::max(0, std::min(7, force));
        gain_percent = clamp_percent(gain_percent);

        const long long numerator =
            static_cast<long long>(0x7fff) *
            (7 - force) *
            gain_percent;

        return static_cast<int>((numerator + 350) / 700);
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
        if (!g_tyre_slip_active)
            return g_centering_percent;

        return scale_value(
            g_centering_percent,
            g_tyre_slip_spring_percent);
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

        return true;
    }

    static bool ensure_initialized()
    {
        if (g_supported)
            return true;

        if (g_init_attempted)
            return false;

        return init(0x7fff, 0, 50);
    }

    bool init(int max_force, int min_force, int)
    {
        if (g_supported)
            return true;

        config.seed_ffb_tuning_defaults();

        g_init_attempted = true;
        (void)max_force;
        (void)min_force;

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

    int set(
        int xdirection,
        int force,
        const std::source_location& source)
    {
        if (!ensure_initialized() || !g_enabled || g_constant_effect < 0)
            return -1;

        tune_offroad_command(xdirection, force, source);

        const int tuned_gain =
            clamp_percent(tuned_gain_for_source(source));

        int signed_level = 0;

        if (g_offroad_level_override_active)
        {
            signed_level =
                scale_value(g_offroad_level_override, tuned_gain);
        }
        else
        {
            const int normalized_force =
                normalized_force_for_source(force, source);
            const int magnitude =
                constant_force_level(normalized_force, tuned_gain);

            if (xdirection < 0x08)
                signed_level = -magnitude;
            else if (xdirection > 0x08)
                signed_level = magnitude;
        }

        SDL_HapticEffect effect{};
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.delay = 0;

        if (g_is_wheel)
        {
            effect.constant.direction = steering_direction();
            effect.constant.level = static_cast<Sint16>(signed_level);
        }
        else
        {
            effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
            effect.constant.direction.dir[0] = signed_level < 0 ? -1 : 1;
            effect.constant.level = static_cast<Sint16>(
                signed_level == 0 ? 0 : std::abs(signed_level));
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

        if (signed_level == 0)
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

    void set_centering_strength(
        int percent,
        const std::source_location& source)
    {
        g_centering_percent = std::max(
            0,
            tuned_centering_for_source(percent, source));

        if (!ensure_initialized() || !g_enabled || g_spring_effect < 0)
            return;

        const int effective_percent =
            clamp_percent(effective_centering_percent());

        if (effective_percent == g_applied_centering_percent)
            return;

        if (effective_percent == 0)
        {
            SDL_HapticStopEffect(g_haptic, g_spring_effect);
            g_applied_centering_percent = 0;
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
            return;
        }

        g_applied_centering_percent = effective_percent;
    }

    static bool create_tyre_slip_effect()
    {
        if ((g_haptic_caps & SDL_HAPTIC_SINE) == 0)
            return false;

        SDL_HapticEffect effect{};
        effect.type = SDL_HAPTIC_SINE;
        effect.periodic.direction = steering_direction();
        effect.periodic.length = SDL_HAPTIC_INFINITY;
        effect.periodic.period = 45;
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

    void set_tyre_slip(
        bool active,
        const std::source_location& source)
    {
        if (!ensure_initialized() || !g_enabled)
            active = false;

        if (active)
        {
            g_tyre_slip_prestart =
                source_function_contains(source, "update_prestart_sine");
            g_tyre_slip_strength_percent =
                g_tyre_slip_prestart
                    ? effect_setting("start_rev_shake", 11)
                    : effect_setting("tyre_slip", 11);
            g_tyre_slip_spring_percent =
                spring_setting("sliding", 67);
        }

        const bool state_changed = active != g_tyre_slip_active;
        g_tyre_slip_active = active;

        if (source_file_contains(source, "ooutputs_base.cpp") &&
            source_function_contains(source, "tick"))
        {
            set_centering_strength(g_centering_percent, source);
        }
        else if (state_changed)
        {
            set_centering_strength(g_centering_percent);
        }

        if (!state_changed)
            return;

        if (!active)
        {
            if (g_tyre_slip_effect >= 0)
                SDL_HapticStopEffect(g_haptic, g_tyre_slip_effect);
            g_tyre_slip_prestart = false;
            return;
        }

        if (g_tyre_slip_effect < 0 && !create_tyre_slip_effect())
            return;

        int effective_percent =
            master_effect_gain(g_tyre_slip_strength_percent);

        // During the start countdown g_gain_percent is deliberately a pure
        // 0..100 throttle/ramp envelope supplied by OOutputs. It never contains
        // the master strength, so there is no double scaling here.
        if (g_tyre_slip_prestart)
        {
            effective_percent =
                scale_value(
                    effective_percent,
                    clamp_percent(g_gain_percent));
        }

        const int magnitude =
            std::max(0, std::min(0x7fff,
                scale_value(0x7fff, effective_percent)));

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
            g_tyre_slip_prestart = false;
            g_applied_centering_percent = -1;
        }
        else
        {
            g_applied_centering_percent = -1;
            set_centering_strength(g_centering_percent);
        }
    }

    void set_gain(int percent)
    {
        g_gain_percent = std::max(0, std::min(100, percent));
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
        g_tyre_slip_prestart = false;
        g_applied_centering_percent = -1;
        g_tyre_slip_strength_percent = 15;
        g_tyre_slip_spring_percent = 67;
        g_offroad_pull_direction = 0;
        g_offroad_level_override_active = false;
        g_offroad_level_override = 0;
        g_logged_constant_update_error = false;
        g_logged_constant_run_error = false;
    }

    bool is_supported()
    {
        return g_supported || ensure_initialized();
    }
}

#else

namespace forcefeedback
{
    bool init(int, int, int) { return false; }
    int set(int, int, const std::source_location&) { return -1; }
    void close() {}
    bool is_supported() { return false; }
    void set_centering_strength(int, const std::source_location&) {}
    void set_tyre_slip(bool, const std::source_location&) {}
    void set_enabled(bool) {}
    void set_gain(int) {}
    void stop() {}
}

#endif