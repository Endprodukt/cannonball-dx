/***************************************************************************
    CannonBall DX Force Feedback wrapper.

    Keep the established backend in ffeedback_base.cpp and layer the unified
    engine/rev sine behaviour on top. This avoids duplicating the large FFB
    implementation while allowing the engine vibration to own the periodic
    channel cleanly between the start grid and normal driving.
***************************************************************************/

// Preserve the existing backend implementation under a private periodic entry
// point. All other forcefeedback symbols retain their original names.
#define set_tyre_slip set_tyre_slip_base
#include "ffeedback_base.cpp"
#undef set_tyre_slip

namespace forcefeedback
{
#if defined(_WIN32)
    static bool engine_source(
        const std::source_location& source)
    {
        return source_function_contains(source, "update_prestart_sine");
    }

    static bool engine_vibration_request(
        bool active,
        const std::source_location& source)
    {
        return active && engine_source(source);
    }

    static bool engine_channel_owned()
    {
        // While the engine sine runs we deliberately leave the normal
        // g_tyre_slip_active flag false. g_tyre_slip_prestart therefore acts as
        // the ownership marker without making the spring think the tyres slide.
        return g_tyre_slip_prestart && !g_tyre_slip_active;
    }

    static bool clean_engine_driving_state()
    {
        return
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

    static void apply_engine_sine_parameters(
        const std::source_location& source)
    {
        if (!g_haptic || g_tyre_slip_effect < 0)
            return;

        // The Ferrari rev counter spans roughly 0x000..0x130 in the value used
        // by the HUD. Convert that to a stable 0..100 RPM factor.
        int revs = static_cast<int>(oferrari.revs >> 16);
        if (revs < 0)
            revs = 0;
        else if (revs > 0x130)
            revs = 0x130;

        const int rpm_percent =
            (revs * 100 + 0x98) / 0x130;

        // Base curve: roughly 10.5 Hz at low RPM to 18.2 Hz at maximum RPM
        // (95 ms -> 55 ms). ENGINE SPEED scales the complete curve while
        // preserving the RPM relationship. The stored 0..100 tuning value maps
        // to 50..150%, with 50 stored as the neutral/default 100% speed.
        const int base_period_ms =
            95 - ((40 * rpm_percent + 50) / 100);

        const int engine_speed_percent =
            50 + effect_setting("engine_speed", 50);

        const int period_ms = std::max(
            20,
            std::min(
                250,
                (base_period_ms * 100 + (engine_speed_percent / 2)) /
                    engine_speed_percent));

        int effective_percent =
            master_effect_gain(effect_setting("start_rev_shake", 11));

        // g_gain_percent is the start-grid throttle ramp or the in-race RPM
        // amplitude envelope supplied by the unified engine-vibration caller.
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
        const bool engine_request =
            engine_vibration_request(active, source);

        // During clean driving the engine owns the already-running periodic
        // effect. Ignore ordinary OFF calls, including the legacy tyre-slip OFF
        // sent every OOutputs frame and the engine caller's own parameter refresh.
        if (!active &&
            engine_channel_owned() &&
            clean_engine_driving_state())
        {
            return;
        }

        // Leaving clean engine operation (crash, off-road, game transition or
        // lifting out of the start-grid rev effect) must really stop the sine.
        if (!active && engine_channel_owned())
        {
            stop_owned_engine_channel();
            restore_engine_centering(source);
            return;
        }

        // Real tyre slip takes ownership immediately. The engine effect already
        // uses the same SDL effect slot, so clearing the ownership marker lets
        // the preserved backend update that slot in place with tyre-slip values.
        if (active && !engine_request && engine_channel_owned())
        {
            g_tyre_slip_prestart = false;
            g_tyre_slip_active = false;
        }

        set_tyre_slip_base(active, source);

        if (engine_request && active)
            apply_engine_sine_parameters(source);
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
