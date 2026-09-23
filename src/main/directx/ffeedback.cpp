/***************************************************************************
    CannonBall DX Force Feedback wrapper.

    Keep the established backend in ffeedback_base.cpp and layer the driving
    engine sine behaviour on top. The original start-grid rev shake stays on
    the preserved backend so it can keep its own strength and timing.

    Linux note:
    The modern DX backend is implemented with SDL2 Haptics rather than native
    DirectInput calls. Reuse that backend on Linux as well so both desktop
    platforms share the same constant-force, spring and periodic-effect model.
    The legacy evdev backend remains preserved in ffeedback_base.cpp for now.
***************************************************************************/

// ffeedback_base.cpp historically selects the simple evdev implementation when
// __linux__ is defined and the modern SDL Haptics implementation when _WIN32 is
// defined. The SDL implementation itself is platform-neutral. Pre-include its
// dependencies with the real Linux platform macros, then select that backend
// only while the preserved implementation is compiled. This keeps platform
// macros correct everywhere outside this one legacy selection point and lets us
// prove Linux parity without duplicating the large tuned FFB implementation.
#if defined(__linux__)
#include "ffeedback.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <SDL.h>
#include "main.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinitengine.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"

#define CANNONBALL_DX_LINUX_SDL_FFB 1
#undef __linux__
#define _WIN32 1
#endif

// Preserve the existing backend implementation under a private periodic entry
// point. All other forcefeedback symbols retain their original names.
#define set_tyre_slip set_tyre_slip_base
#include "ffeedback_base.cpp"
#undef set_tyre_slip

#if defined(CANNONBALL_DX_LINUX_SDL_FFB)
#undef _WIN32
#define __linux__ 1
#endif

namespace forcefeedback
{
#if defined(_WIN32) || defined(CANNONBALL_DX_LINUX_SDL_FFB)
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
