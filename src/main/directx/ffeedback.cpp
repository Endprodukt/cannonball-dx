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
    static bool g_logged_constant_update_error = false;
    static bool g_logged_constant_run_error = false;

    static unsigned int g_haptic_caps = 0;
    static int g_max_force = 0x7fff;
    static int g_min_force = 0x2fff;
    static int g_gain_percent = 100;
    static int g_centering_percent = 30;
    static int g_applied_centering_percent = -1;
    static int g_tyre_slip_strength_percent = 15;
    static int g_tyre_slip_spring_percent = 67;
    static int g_offroad_pull_direction = 0;

    // DirectInput constant-force magnitude is nominally 0..10000, whereas
    // SDL_HapticConstant uses the full signed 16-bit range. The legacy DX
    // backend generated its force value from the 0x7fff/0x2fff table and then
    // clamped it to DI_FFNOMINALMAX (10000). Preserve that effective strength
    // before converting to SDL's 0..32767 range. Without this conversion small
    // effects are only ~30% of their previous physical strength.
    static const int LEGACY_DI_NOMINAL_MAX = 10000;

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
        return scale_value(config.controls.ffb_strength, effect_percent);
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
        // Rough-surface grit currently uses a small absolute gain rather than
        // the master FFB percentage. Keeping the default at 4 reproduces the
        // exact tested DX setting; users can raise it directly in config.xml.
        if (source_function_contains(source, "apply_surface_rattle"))
            return effect_setting("sand", 4);

        if (source_function_contains(source, "set_shift_force"))
            return master_effect_gain(effect_setting("gear_shift", 85));

        if (source_function_contains(source, "set_crash_yank"))
        {
            return master_effect_gain(
                ocrash.is_flip()
                    ? effect_setting("crash_flip", 115)
                    : effect_setting("crash_spin", 115));
        }

        if (source_function_contains(source, "set_start_intro_force"))
        {
            return scale_value(
                g_gain_percent,
                effect_setting("start_steering", 100));
        }

        // Music Select's current detent is already tuned around an 80% scale.
        // Treat the XML value as that visible tuning number: 80 reproduces the
        // current build, 40 halves it and 100 makes it 25% stronger.
        if (source_function_contains(source, "apply_music_detent_ffb") ||
            source_function_contains(source, "set_music_detent_force"))
        {
            return scale_value(
                g_gain_percent,
                effect_setting("music_selector", 80),
                80);
        }

        if (source_function_contains(source, "do_motors") &&
            ocrash.skid_counter)
        {
            return master_effect_gain(effect_setting("traffic_skid", 100));
        }

        if (source_function_contains(source, "apply_crash_ffb_force") &&
            ocrash.crash_counter)
        {
            if (low_speed_crash_bump())
                return master_effect_gain(effect_setting("crash_bump", 100));

            if (ocrash.is_flip())
            {
                if (ocrash.crash_state >= 5)
                {
                    return master_effect_gain(
                        effect_setting("crash_flip_landing", 100));
                }

                return master_effect_gain(
                    effect_setting("crash_flip_impact", 100));
            }

            return master_effect_gain(
                effect_setting("crash_spin_impact", 100));
        }

        return g_gain_percent;
    }

    static void tune_offroad_command(
        int& xdirection,
        int& force,
        const std::source_location& source)
    {
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

        // OOutputs has already combined its alternating off-road rumble with
        // the outward pull. Recover those two current components, rescale each
        // independently, then combine them again. The defaults reproduce the
        // existing 50/100% rumble and 3/7 or 2/7 pull exactly.
        int combined_force = xdirection - 0x08;
        const int current_pull_magnitude = fully_offroad ? 2 : 3;
        const int current_pull =
            current_pull_magnitude * g_offroad_pull_direction;
        const int current_rumble =
            combined_force - current_pull;

        const int default_rumble = fully_offroad ? 100 : 50;
        const int configured_rumble =
            fully_offroad
                ? effect_setting("offroad_rumble_full", 100)
                : effect_setting("offroad_rumble_one_wheel", 50);

        int tuned_rumble =
            scale_value(current_rumble, configured_rumble, default_rumble);

        // The inherited motor command has only seven signed force steps. At
        // low one-wheel strengths a real alternating impulse could round to
        // zero and disappear completely. Keep the weakest signed step whenever
        // the effect is enabled and the source rumble is actually non-zero.
        if (configured_rumble > 0 &&
            current_rumble != 0 &&
            tuned_rumble == 0)
        {
            tuned_rumble = current_rumble < 0 ? -1 : 1;
        }

        const int pull_percent =
            fully_offroad
                ? effect_setting("offroad_pull_full", 29)
                : effect_setting("offroad_pull_one_wheel", 43);
        const int tuned_pull_magnitude =
            scale_value(7, pull_percent);
        const int tuned_pull =
            tuned_pull_magnitude * g_offroad_pull_direction;

        combined_force = tuned_rumble + tuned_pull;
        combined_force = std::max(-7, std::min(7, combined_force));

        xdirection = 0x08 + combined_force;
        force = combined_force == 0
            ? 7
            : 7 - std::abs(combined_force);
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
        // The normal frontend, Attract Mode and stationary/start states all use
        // the configured low-speed spring. This also overrides the inherited
        // menu code that used to send centering_strength directly.
        if (source_file_contains(source, "menu.cpp") ||
            source_file_contains(source, "menu_base.cpp") ||
            source_function_contains(source, "reset_music_detent_ffb"))
        {
            return configured_low_speed_spring();
        }

        if (source_function_contains(source, "apply_music_detent_ffb"))
        {
            return scale_value(
                requested_percent,
                effect_setting("music_selector", 80),
                80);
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
        gain_percent = std::max(0, gain_percent);

        // Match the exact integer order used by the previous DirectInput path.
        int legacy_magnitude =
            g_max_force -
            (((g_max_force - g_min_force) / 7) * force);

        legacy_magnitude =
            static_cast<int>(
                (static_cast<long long>(legacy_magnitude) * gain_percent) /
                100);

        legacy_magnitude =
            std::max(0, std::min(LEGACY_DI_NOMINAL_MAX, legacy_magnitude));

        // Convert the old 0..10000 DirectInput percentage to SDL's 0..32767.
        return static_cast<int>(
            (static_cast<long long>(legacy_magnitude) * 0x7fff +
             (LEGACY_DI_NOMINAL_MAX / 2)) /
            LEGACY_DI_NOMINAL_MAX);
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

        // Materialize every documented tuning value into the shared config
        // tree. Old configs that do not contain them therefore behave exactly
        // like the current DX preset and gain the fields on the next save.
        config.seed_ffb_tuning_defaults();

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

    int set(
        int xdirection,
        int force,
        const std::source_location& source)
    {
        if (!ensure_initialized() || !g_enabled || g_constant_effect < 0)
            return -1;

        tune_offroad_command(xdirection, force, source);

        const int tuned_gain = tuned_gain_for_source(source);
        const int magnitude = constant_force_level(force, tuned_gain);

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

    void set_tyre_slip(
        bool active,
        const std::source_location& source)
    {
        if (!ensure_initialized() || !g_enabled)
            active = false;

        if (active)
        {
            g_tyre_slip_strength_percent =
                source_function_contains(source, "update_prestart_sine")
                    ? effect_setting("start_rev_shake", 15)
                    : effect_setting("tyre_slip", 15);
            g_tyre_slip_spring_percent =
                spring_setting("sliding", 67);
        }

        const bool state_changed = active != g_tyre_slip_active;
        g_tyre_slip_active = active;

        // OOutputs calls this once per game tick before its own cached spring
        // update. Re-evaluate the configured curve here so custom speed_start /
        // speed_full values are honoured even where the inherited 100..240
        // spring cache would otherwise suppress an update.
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
            return;
        }

        if (g_tyre_slip_effect < 0 && !create_tyre_slip_effect())
            return;

        const int magnitude =
            std::max(0, std::min(0x7fff,
                static_cast<int>(
                    (static_cast<long long>(0x7fff) *
                     g_tyre_slip_strength_percent *
                     g_gain_percent) /
                    10000)));

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
        g_applied_centering_percent = -1;
        g_tyre_slip_strength_percent = 15;
        g_tyre_slip_spring_percent = 67;
        g_offroad_pull_direction = 0;
        g_logged_constant_update_error = false;
        g_logged_constant_run_error = false;
    }

    bool is_supported()
    {
        // Probe lazily so a rumble-capable gamepad can coexist with a separate
        // FFB wheel. Wheel haptics and gamepad rumble are independent paths.
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