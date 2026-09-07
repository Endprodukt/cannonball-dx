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
    static bool engine_vibration_request(
        bool active,
        const std::source_location& source)
    {
        return active &&
            source_function_contains(source, "update_prestart_sine");
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
        // Temporarily hide the periodic-channel active flag while applying the
        // normal requested spring, then restore the actual running state.
        const bool periodic_active = g_tyre_slip_active;
        g_tyre_slip_active = false;
        set_centering_strength(g_centering_percent, source);
        g_tyre_slip_active = periodic_active;
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

        // Deliberately slower and heavier than tyre slip: about 10.5 Hz at the
        // bottom of the range, building to about 18 Hz at maximum revs. This
        // keeps the effect engine-like instead of turning into a high-frequency
        // steering-wheel buzz, while still clearly speeding up with RPM.
        const int period_ms =
            95 - ((40 * rpm_percent + 50) / 100);

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

        restore_engine_centering(source);
    }

    void set_tyre_slip(
        bool active,
        const std::source_location& source)
    {
        const bool engine_request =
            engine_vibration_request(active, source);

        const bool output_tick_request =
            source_file_contains(source, "ooutputs_base.cpp") &&
            source_function_contains(source, "tick");

        // Normal OOutputs::tick() sends tyre-slip OFF every clean driving frame.
        // Once the engine owns the shared periodic channel, that OFF must not
        // kill it. Crash, off-road and real tyre slip are deliberately excluded
        // so those effects can still take ownership immediately.
        if (!active &&
            g_tyre_slip_active &&
            g_tyre_slip_prestart &&
            output_tick_request &&
            clean_engine_driving_state())
        {
            restore_engine_centering(source);
            return;
        }

        // A real tyre-slip request arriving while the engine sine is running is
        // a mode change, not a no-op active->active transition. Stop once so the
        // preserved backend rebuilds the correct tyre-slip strength and spring.
        if (active &&
            !engine_request &&
            g_tyre_slip_active &&
            g_tyre_slip_prestart)
        {
            set_tyre_slip_base(false, source);
        }

        set_tyre_slip_base(active, source);

        if (engine_request && active && g_tyre_slip_active)
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
