/***************************************************************************
    CannonBall DX Force Feedback wrapper.

    Keep the established backend in ffeedback_base.cpp and layer the driving
    engine sine behaviour on top. The original start-grid rev shake stays on
    the preserved backend so it can keep its own strength and timing.
***************************************************************************/

#if defined(_WIN32)
#include <windows.h>
#include <SDL.h>

#include <iostream>
#include <string>

namespace cannonball_logitech_range_test
{
    using LogiSteeringInitializeFn = bool (__cdecl *)(bool);
    using LogiSteeringShutdownFn = void (__cdecl *)();
    using LogiUpdateFn = bool (__cdecl *)();
    using LogiIsConnectedFn = bool (__cdecl *)(int);
    using LogiGetOperatingRangeFn = bool (__cdecl *)(int, int*);
    using LogiSetOperatingRangeFn = bool (__cdecl *)(int, int);
    using LogiPlaySoftstopForceFn = bool (__cdecl *)(int, int);
    using LogiStopSoftstopForceFn = bool (__cdecl *)(int);

    static HMODULE g_sdk = nullptr;
    static LogiSteeringInitializeFn g_initialize = nullptr;
    static LogiSteeringShutdownFn g_shutdown = nullptr;
    static LogiUpdateFn g_update = nullptr;
    static LogiIsConnectedFn g_is_connected = nullptr;
    static LogiGetOperatingRangeFn g_get_range = nullptr;
    static LogiSetOperatingRangeFn g_set_range = nullptr;
    static LogiPlaySoftstopForceFn g_play_softstop = nullptr;
    static LogiStopSoftstopForceFn g_stop_softstop = nullptr;

    static bool g_sdk_initialized = false;
    static bool g_softstop_active = false;
    static int g_saved_range = 0;
    static bool g_logged_missing_sdk = false;

    template <typename T>
    static T load_symbol(const char* name)
    {
        return reinterpret_cast<T>(GetProcAddress(g_sdk, name));
    }

    static bool load_sdk()
    {
        if (g_sdk)
            return true;

#ifdef _WIN64
        const wchar_t* dll_name = L"logi_steering_wheel_x64.dll";
#else
        const wchar_t* dll_name = L"logi_steering_wheel_x86.dll";
#endif

        wchar_t program_files[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableW(
            L"ProgramW6432", program_files, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            length = GetEnvironmentVariableW(
                L"ProgramFiles", program_files, MAX_PATH);
        }

        if (length > 0 && length < MAX_PATH)
        {
            std::wstring sdk_path(program_files);
            sdk_path += L"\\Logi\\wheel_sdk\\9_1_0\\";
            sdk_path += dll_name;
            g_sdk = LoadLibraryW(sdk_path.c_str());
        }

        // Also allow a local SDK DLL for developers or older G HUB layouts.
        if (!g_sdk)
            g_sdk = LoadLibraryW(dll_name);

        if (!g_sdk)
        {
            if (!g_logged_missing_sdk)
            {
                std::cout
                    << "SDL FFB: Logitech wheel SDK not found; "
                    << "using normal SDL haptics" << std::endl;
                g_logged_missing_sdk = true;
            }
            return false;
        }

        g_initialize =
            load_symbol<LogiSteeringInitializeFn>("LogiSteeringInitialize");
        g_shutdown =
            load_symbol<LogiSteeringShutdownFn>("LogiSteeringShutdown");
        g_update = load_symbol<LogiUpdateFn>("LogiUpdate");
        g_is_connected =
            load_symbol<LogiIsConnectedFn>("LogiIsConnected");
        g_get_range =
            load_symbol<LogiGetOperatingRangeFn>("LogiGetOperatingRange");
        g_set_range =
            load_symbol<LogiSetOperatingRangeFn>("LogiSetOperatingRange");
        g_play_softstop =
            load_symbol<LogiPlaySoftstopForceFn>("LogiPlaySoftstopForce");
        g_stop_softstop =
            load_symbol<LogiStopSoftstopForceFn>("LogiStopSoftstopForce");

        if (!g_initialize || !g_shutdown || !g_update || !g_is_connected ||
            !g_get_range || !g_set_range || !g_play_softstop ||
            !g_stop_softstop)
        {
            std::cout
                << "SDL FFB: Logitech wheel SDK is missing required exports; "
                << "using normal SDL haptics" << std::endl;
            FreeLibrary(g_sdk);
            g_sdk = nullptr;
            return false;
        }

        return true;
    }

    static void shutdown_sdk()
    {
        if (g_softstop_active && g_stop_softstop)
            g_stop_softstop(0);

        g_softstop_active = false;
        g_saved_range = 0;

        if (g_sdk_initialized && g_shutdown)
            g_shutdown();

        g_sdk_initialized = false;

        if (g_sdk)
        {
            FreeLibrary(g_sdk);
            g_sdk = nullptr;
        }

        g_initialize = nullptr;
        g_shutdown = nullptr;
        g_update = nullptr;
        g_is_connected = nullptr;
        g_get_range = nullptr;
        g_set_range = nullptr;
        g_play_softstop = nullptr;
        g_stop_softstop = nullptr;
    }

    static bool capture_current_range(SDL_Joystick* joystick)
    {
        if (!joystick ||
            SDL_JoystickGetType(joystick) != SDL_JOYSTICK_TYPE_WHEEL ||
            SDL_JoystickGetVendor(joystick) != 0x046d)
        {
            return false;
        }

        if (!load_sdk())
            return false;

        if (!g_initialize(false))
        {
            std::cout
                << "SDL FFB: Logitech SDK initialization failed; "
                << "using normal SDL haptics" << std::endl;
            shutdown_sdk();
            return false;
        }

        g_sdk_initialized = true;
        g_update();

        // This test branch targets the common single Logitech-wheel setup.
        // The public SDK uses controller index 0 for the first connected wheel.
        if (!g_is_connected(0))
        {
            std::cout
                << "SDL FFB: Logitech SDK did not report controller 0; "
                << "using normal SDL haptics" << std::endl;
            shutdown_sdk();
            return false;
        }

        int range = 0;
        if (!g_get_range(0, &range) || range < 40 || range > 2700)
        {
            std::cout
                << "SDL FFB: unable to read Logitech operating range; "
                << "using normal SDL haptics" << std::endl;
            shutdown_sdk();
            return false;
        }

        g_saved_range = range;
        std::cout
            << "SDL FFB: captured Logitech operating range: "
            << g_saved_range << " degrees" << std::endl;
        return true;
    }

    static void restore_range_and_softstop()
    {
        if (!g_sdk_initialized || g_saved_range <= 0)
            return;

        g_update();

        const bool range_restored =
            g_set_range(0, g_saved_range);
        const bool softstop_started =
            range_restored && g_play_softstop(0, 100);

        g_softstop_active = softstop_started;

        std::cout
            << "SDL FFB: Logitech range restore "
            << (range_restored ? "OK" : "FAILED")
            << ", soft stop "
            << (softstop_started ? "OK" : "FAILED")
            << ", range=" << g_saved_range << " degrees"
            << std::endl;
    }

    static SDL_Haptic* open_haptic_preserving_range(SDL_Joystick* joystick)
    {
        const bool preserve_logitech_range =
            capture_current_range(joystick);

        SDL_Haptic* haptic = SDL_HapticOpenFromJoystick(joystick);
        if (!haptic)
        {
            if (preserve_logitech_range)
                shutdown_sdk();
            return nullptr;
        }

        if (preserve_logitech_range)
            restore_range_and_softstop();

        return haptic;
    }

    static int set_autocenter_and_restore(SDL_Haptic* haptic, int autocenter)
    {
        const int result = SDL_HapticSetAutocenter(haptic, autocenter);

        // The normal CannonBall backend deliberately disables SDL autocenter.
        // Logitech's G HUB soft stop can disappear at that point, so re-apply
        // the user's captured operating range and Logitech soft stop afterwards.
        if (g_sdk_initialized && g_saved_range > 0)
            restore_range_and_softstop();

        return result;
    }

    static void close_haptic(SDL_Haptic* haptic)
    {
        if (g_sdk_initialized)
            shutdown_sdk();

        SDL_HapticClose(haptic);
    }
}

#define SDL_HapticOpenFromJoystick \
    cannonball_logitech_range_test::open_haptic_preserving_range
#define SDL_HapticSetAutocenter \
    cannonball_logitech_range_test::set_autocenter_and_restore
#define SDL_HapticClose cannonball_logitech_range_test::close_haptic
#endif

// Preserve the existing backend implementation under a private periodic entry
// point. All other forcefeedback symbols retain their original names.
#define set_tyre_slip set_tyre_slip_base
#include "ffeedback_base.cpp"
#undef set_tyre_slip

#if defined(_WIN32)
#undef SDL_HapticOpenFromJoystick
#undef SDL_HapticSetAutocenter
#undef SDL_HapticClose
#endif

namespace forcefeedback
{
#if defined(_WIN32)
    static bool start_rev_source(
        const std::source_location& source)
    {
        return
            source_function_contains(source, "update_prestart_sine") &&
            !source_function_contains(source, "update_prestart_sine_engine_vibration");
    }

    static bool driving_engine_source(
        const std::source_location& source)
    {
        return source_function_contains(
            source,
            "update_prestart_sine_engine_vibration");
    }

    static bool engine_channel_owned()
    {
        // While the driving engine sine runs we deliberately leave the normal
        // g_tyre_slip_active flag false. g_tyre_slip_prestart therefore acts as
        // the ownership marker without making the spring think the tyres slide.
        return g_tyre_slip_prestart && !g_tyre_slip_active;
    }

    static bool clean_engine_driving_state()
    {
        return
            cannonball::state == cannonball::STATE_GAME &&
            outrun.game_state == GS_INGAME &&
            !outrun.SkiddingOnRoad() &&
            !ocrash.crash_counter &&
            !ocrash.skid_counter &&
            oferrari.wheel_state == OFerrari::WHEELS_ON;
    }

    static void restore_engine_centering(
        const std::source_location& source)
    {
        // Engine vibration must not inherit the tyre-slip spring reduction.
        const bool periodic_active = g_tyre_slip_active;
        g_tyre_slip_active = false;
        set_centering_strength(g_centering_percent, source);
        g_tyre_slip_active = periodic_active;
    }

    static void stop_owned_engine_channel()
    {
        if (g_haptic && g_tyre_slip_effect >= 0)
            SDL_HapticStopEffect(g_haptic, g_tyre_slip_effect);

        g_tyre_slip_active = false;
        g_tyre_slip_prestart = false;
    }

    static void apply_driving_engine_sine_parameters(
        const std::source_location& source)
    {
        if (!g_haptic || g_tyre_slip_effect < 0)
            return;

        // OutRun's converted engine-rev value normally bottoms out around 0x1F
        // and reaches roughly 0x130 near the top of the useful rev range.
        int revs = static_cast<int>(oferrari.revs >> 16);
        const int idle_revs = 0x1F;
        const int max_revs = 0x130;

        if (revs < idle_revs)
            revs = idle_revs;
        else if (revs > max_revs)
            revs = max_revs;

        // ENGINE PERIOD is the low-RPM/base sine period. Frequency should track
        // engine speed, so period is inversely proportional to RPM:
        //
        //     period = base_period * idle_revs / current_revs
        //
        // With the 110 ms default this gives about 110 ms at idle and about
        // 11 ms near maximum revs instead of the old shallow 45% reduction.
        // The user-facing range is 10..250 ms; the lower clamp also prevents
        // extremely high periodic frequencies on devices that dislike them.
        const int base_period_ms = config.engine_period_ms();
        const int scaled_period_ms =
            (base_period_ms * idle_revs + (revs / 2)) / revs;
        const int period_ms = std::max(
            Config::ENGINE_PERIOD_MIN_MS,
            std::min(
                Config::ENGINE_PERIOD_MAX_MS,
                scaled_period_ms));

        int effective_percent =
            master_effect_gain(config.engine_vibration_strength());

        // g_gain_percent is the in-race RPM amplitude envelope supplied by the
        // driving engine-vibration caller.
        effective_percent =
            scale_value(
                effective_percent,
                clamp_percent(g_gain_percent));

        const int magnitude =
            std::max(0, std::min(0x7fff,
                scale_value(0x7fff, effective_percent)));

        SDL_HapticEffect effect{};
        effect.type = SDL_HAPTIC_SINE;
        effect.periodic.direction = steering_direction();
        effect.periodic.length = SDL_HAPTIC_INFINITY;
        effect.periodic.period = static_cast<Uint16>(period_ms);
        effect.periodic.magnitude = static_cast<Sint16>(magnitude);

        if (SDL_HapticUpdateEffect(g_haptic, g_tyre_slip_effect, &effect) == 0)
            SDL_HapticRunEffect(g_haptic, g_tyre_slip_effect, 1);

        // The physical periodic effect keeps running, but logically there is no
        // tyre slip. This prevents the engine vibration from weakening the
        // normal speed-dependent centering spring.
        g_tyre_slip_active = false;
        g_tyre_slip_prestart = true;
        restore_engine_centering(source);
    }

    void set_tyre_slip(
        bool active,
        const std::source_location& source)
    {
        const bool grid_rev_request =
            active && start_rev_source(source);
        const bool driving_engine_request =
            active && driving_engine_source(source);
        const bool engine_related_request =
            grid_rev_request || driving_engine_request;

        // During clean driving the engine owns the already-running periodic
        // effect. Ignore ordinary OFF calls only while CannonBall is genuinely
        // still in gameplay. Entering the frontend/menu must stop immediately.
        if (!active &&
            engine_channel_owned() &&
            clean_engine_driving_state())
        {
            return;
        }

        // Leaving clean engine operation (crash, off-road, menu/game transition)
        // must really stop the driving engine sine.
        if (!active && engine_channel_owned())
        {
            stop_owned_engine_channel();
            restore_engine_centering(source);
            return;
        }

        // Real tyre slip takes ownership immediately. The driving engine effect
        // already uses the same SDL effect slot, so clearing the ownership
        // marker lets the preserved backend update that slot in place.
        if (active && !engine_related_request && engine_channel_owned())
        {
            g_tyre_slip_prestart = false;
            g_tyre_slip_active = false;
        }

        // The original start-grid rev shake is intentionally handled entirely
        // by the preserved backend: start_rev_shake controls its strength and
        // its original 45 ms SINE period stays unchanged. Only the in-race
        // engine request is replaced with the RPM-dependent motor curve.
        set_tyre_slip_base(active, source);

        if (driving_engine_request)
            apply_driving_engine_sine_parameters(source);
    }
#else
    void set_tyre_slip(
        bool active,
        const std::source_location& source)
    {
        set_tyre_slip_base(active, source);
    }
#endif
}
