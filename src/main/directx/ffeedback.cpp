/***************************************************************************
    CannonBall DX Force Feedback wrapper.

    Keep the established backend in ffeedback_base.cpp and layer the driving
    engine sine behaviour on top. The original start-grid rev shake stays on
    the preserved backend so it can keep its own strength and timing.
***************************************************************************/

// Pre-include the Windows backend dependencies before temporarily renaming the
// generic `set` symbol below. This keeps the macro away from std::set and other
// declarations in standard/project headers.
#include "ffeedback.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <SDL.h>
#include "main.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinitengine.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"

namespace forcefeedback
{
    void set_centering_strength_base(
        int percent,
        const std::source_location& source);

    inline void set_centering_strength_base(int percent)
    {
        set_centering_strength_base(
            percent,
            std::source_location::current());
    }
}

#define set set_base
#define set_centering_strength set_centering_strength_base
#define set_tyre_slip set_tyre_slip_base
#else
#define set_tyre_slip set_tyre_slip_base
#endif

#include "ffeedback_base.cpp"

#undef set_tyre_slip
#if defined(_WIN32)
#undef set_centering_strength
#undef set
#endif

namespace forcefeedback
{
#if defined(_WIN32)
    static bool simple_gameplay_active()
    {
        return
            cannonball::state == cannonball::STATE_GAME &&
            outrun.game_state == GS_INGAME;
    }

    static bool simple_classic_centering_request(
        const std::source_location& source)
    {
        // In SIMPLE mode the SDL spring is the only centering force. The
        // original moving-cabinet motor tables still provide crash/skid/offroad
        // effects, but their normal clean-road steering commands would otherwise
        // add a second, very strong center force on top of the spring.
        return
            source_function_contains(source, "motor_output") &&
            simple_gameplay_active() &&
            !ocrash.crash_counter &&
            !ocrash.skid_counter &&
            oferrari.wheel_state == OFerrari::WHEELS_ON;
    }

    int set(
        int xdirection,
        int force,
        const std::source_location& source)
    {
        if (!config.ffb_modern_enabled())
        {
            if (!simple_gameplay_active())
            {
                // SIMPLE is a driving-only mode. The original motor table can
                // still tick during frontend/transition states; neutralize it
                // so there are no effects while navigating menus.
                return set_base(0x08, 7, source);
            }

            if (simple_classic_centering_request(source))
            {
                // Centering belongs exclusively to the spring in SIMPLE mode.
                // Keep classic crash/skid/offroad motor commands untouched.
                return set_base(0x08, 7, source);
            }
        }

        return set_base(xdirection, force, source);
    }

    void set_centering_strength(
        int percent,
        const std::source_location& source)
    {
        if (!config.ffb_modern_enabled())
        {
            // SIMPLE uses the user-facing Centering value as a fixed spring,
            // but only while the race is actually active. In particular there
            // must be no spring in the frontend/menu or during transitions.
            set_centering_strength_base(
                simple_gameplay_active() ? clamp_percent(percent) : 0,
                source);
            return;
        }

        // MODERN retains its existing dynamic/tuned spring behaviour verbatim.
        set_centering_strength_base(percent, source);
    }

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
        set_centering_strength_base(g_centering_percent, source);
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

        int revs = static_cast<int>(oferrari.revs >> 16);
        const int idle_revs = 0x1F;
        const int max_revs = 0x130;

        if (revs < idle_revs)
            revs = idle_revs;
        else if (revs > max_revs)
            revs = max_revs;

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

        g_tyre_slip_active = false;
        g_tyre_slip_prestart = true;
        restore_engine_centering(source);
    }

    void set_tyre_slip(
        bool active,
        const std::source_location& source)
    {
        if (!config.ffb_modern_enabled())
        {
            set_tyre_slip_base(false, source);
            return;
        }

        const bool grid_rev_request =
            active && start_rev_source(source);
        const bool driving_engine_request =
            active && driving_engine_source(source);
        const bool engine_related_request =
            grid_rev_request || driving_engine_request;

        if (!active &&
            engine_channel_owned() &&
            clean_engine_driving_state())
        {
            return;
        }

        if (!active && engine_channel_owned())
        {
            stop_owned_engine_channel();
            restore_engine_centering(source);
            return;
        }

        if (active && !engine_related_request && engine_channel_owned())
        {
            g_tyre_slip_prestart = false;
            g_tyre_slip_active = false;
        }

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
