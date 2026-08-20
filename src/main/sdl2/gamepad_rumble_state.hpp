#pragma once

#include <algorithm>
#include <SDL.h>

#include "main.hpp"
#include "frontend/config.hpp"
#include "engine/oinputs.hpp"
#include "engine/omusic.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinitengine.hpp"

// Gamepad rumble is intentionally independent from wheel force feedback.
// Keep the enable state separate from the configured rumble strength so users
// can turn rumble off without losing their preferred intensity.
namespace gamepad_rumble
{
    inline bool enabled = true;

    namespace detail
    {
        inline bool gear_valid = false;
        inline bool last_gear = false;
        inline Uint32 shift_started = 0;

        inline int last_music_selection = -1;
        inline Uint32 music_pulse_started = 0;

        inline int start_level = 0;

        inline int last_crash_counter = 0;
        inline int last_crash_state = 0;
        inline int last_collision_skid = 0;
        inline Uint32 crash_started = 0;
        inline Uint32 landing_started = 0;
        inline Uint32 traffic_hit_started = 0;

        inline Uint16 master_intensity()
        {
            float strength = config.controls.rumble;
            if (strength < 0.0f)
                strength = 0.0f;
            else if (strength > 1.0f)
                strength = 1.0f;

            return static_cast<Uint16>(strength * 65535.0f);
        }

        inline Uint16 scaled(Uint16 master, int percent)
        {
            return static_cast<Uint16>(
                (static_cast<unsigned int>(master) * percent) / 100U);
        }

        inline void reset_tracking()
        {
            gear_valid = false;
            last_gear = false;
            shift_started = 0;

            last_music_selection = -1;
            music_pulse_started = 0;

            start_level = 0;

            last_crash_counter = 0;
            last_crash_state = 0;
            last_collision_skid = 0;
            crash_started = 0;
            landing_started = 0;
            traffic_hit_started = 0;
        }
    }

    // Central SDL GameController rumble mixer. During gameplay the incoming
    // legacy cabinet-motor request is deliberately ignored: rumble is derived
    // directly from game state so wheel FFB can never suppress pad effects.
    inline int dispatch(
        SDL_GameController* controller,
        Uint16 low_frequency,
        Uint16 high_frequency,
        Uint32 duration_ms)
    {
        using namespace detail;

        // Outside gameplay retain normal stop/probe behaviour.
        if (!enabled || cannonball::state != cannonball::STATE_GAME)
        {
            reset_tracking();
            return SDL_GameControllerRumble(
                controller,
                enabled ? low_frequency : 0,
                enabled ? high_frequency : 0,
                enabled ? duration_ms : 0);
        }

        const Uint32 now = SDL_GetTicks();
        const Uint16 master = master_intensity();

        // ------------------------------------------------------------
        // Music selection. Read the real selector position rather than the
        // compatibility getter used to silence the old timed wheel detent.
        // ------------------------------------------------------------
        if (outrun.game_state == GS_MUSIC)
        {
            const int selection = omusic.get_music_position();

            if (last_music_selection < 0)
            {
                last_music_selection = selection;
            }
            else if (selection != last_music_selection)
            {
                last_music_selection = selection;
                music_pulse_started = now;
            }
        }
        else
        {
            last_music_selection = -1;
            music_pulse_started = 0;
        }

        // ------------------------------------------------------------
        // Gear change: one short mechanical bump.
        // ------------------------------------------------------------
        if (outrun.game_state == GS_INGAME)
        {
            if (!gear_valid)
            {
                last_gear = oinputs.gear;
                gear_valid = true;
            }
            else if (oinputs.gear != last_gear)
            {
                last_gear = oinputs.gear;
                shift_started = now;
            }
        }
        else
        {
            gear_valid = false;
            shift_started = 0;
        }

        // ------------------------------------------------------------
        // Start-line rev effect. Full throttle reaches 90% of the configured
        // master and ramps up over roughly 0.3 seconds at 30 logic ticks/sec.
        // ------------------------------------------------------------
        const bool waiting_for_start =
            outrun.game_state == GS_START2 ||
            outrun.game_state == GS_START3 ||
            (outrun.game_state == GS_START1 &&
             oferrari.state == OFerrari::FERRARI_LOGIC);

        int start_target = 0;

        if (waiting_for_start && master > 0)
        {
            int pedal = std::clamp(static_cast<int>(oinputs.acc_adjust), 0, 0xFF);

            if (pedal >= 8)
            {
                start_target =
                    (static_cast<int>(master) * pedal * 90) /
                    (0xFF * 100);
            }
        }

        const int ramp_up = std::max(1, static_cast<int>(master) / 10);
        const int ramp_down = std::max(1, static_cast<int>(master) / 6);

        if (start_level < start_target)
            start_level = std::min(start_target, start_level + ramp_up);
        else if (start_level > start_target)
            start_level = std::max(start_target, start_level - ramp_down);

        // ------------------------------------------------------------
        // Crash / traffic collision tracking.
        // ------------------------------------------------------------
        if (ocrash.crash_counter > 0 && last_crash_counter <= 0)
            crash_started = now;

        if (last_crash_state == 2 && ocrash.crash_state >= 5)
            landing_started = now;

        if (ocrash.skid_counter != 0 && last_collision_skid == 0)
            traffic_hit_started = now;

        last_crash_counter = ocrash.crash_counter;
        last_crash_state = ocrash.crash_state;
        last_collision_skid = ocrash.skid_counter;

        // No dependency on D_MOTOR or the current FFB output mode from here on.
        Uint16 out_low = 0;
        Uint16 out_high = 0;
        Uint32 out_duration = 45;

        // Priority: crash > traffic > shift > music > start > offroad > skid.
        if (master > 0 && ocrash.crash_counter > 0)
        {
            const Uint32 crash_age = now - crash_started;

            if (crash_age < 160)
            {
                out_low = master;
                out_high = master;
            }
            else if (ocrash.crash_state >= 1 && ocrash.crash_state <= 4)
            {
                const bool heavy_phase = ((now / 90U) & 1U) == 0;
                out_low = scaled(master, heavy_phase ? 90 : 60);
                out_high = scaled(master, heavy_phase ? 75 : 40);
            }
            else if (landing_started && now - landing_started < 150)
            {
                out_low = master;
                out_high = master;
            }
            else if (crash_age < 300)
            {
                out_low = scaled(master, 55);
                out_high = scaled(master, 30);
            }
        }
        else if (master > 0 &&
                 traffic_hit_started &&
                 now - traffic_hit_started < 150)
        {
            out_low = scaled(master, 90);
            out_high = scaled(master, 65);
        }
        else if (master > 0 && shift_started && now - shift_started < 95)
        {
            const Uint32 age = now - shift_started;

            if (age < 50)
            {
                out_low = scaled(master, 60);
                out_high = scaled(master, 25);
            }
            else
            {
                out_low = scaled(master, 20);
                out_high = scaled(master, 40);
            }
        }
        else if (master > 0 &&
                 music_pulse_started &&
                 now - music_pulse_started < 130)
        {
            // A tactile but still light selector bump.
            out_low = scaled(master, 20);
            out_high = scaled(master, 55);
        }
        else if (master > 0 && waiting_for_start && start_level > 0)
        {
            out_low = static_cast<Uint16>(start_level / 2);
            out_high = static_cast<Uint16>(start_level);
        }
        else if (master > 0 &&
                 outrun.game_state == GS_INGAME &&
                 oferrari.wheel_state != OFerrari::WHEELS_ON)
        {
            // Direct off-road texture. This intentionally does not use the
            // cabinet D_MOTOR bit, which changes behaviour when wheel FFB is on.
            const int speed = std::clamp(
                static_cast<int>(oinitengine.car_increment >> 16),
                0,
                0x126);

            if (speed > 8)
            {
                int strength_percent = 45 + (speed * 35) / 0x126;

                if (oferrari.wheel_state != OFerrari::WHEELS_OFF)
                    strength_percent -= 12;

                // Uneven pulse gives grass/dirt a rough texture rather than a
                // featureless continuous vibration.
                if (((now / 55U) & 1U) != 0)
                    strength_percent = (strength_percent * 72) / 100;

                out_low = scaled(master, (strength_percent * 65) / 100);
                out_high = scaled(master, strength_percent);
            }
        }
        else if (master > 0 &&
                 outrun.game_state == GS_INGAME &&
                 outrun.SkiddingOnRoad())
        {
            // Preserve the existing tyre-smoke/skid feel independently of FFB.
            out_low = 0;
            out_high = master;
        }

        return SDL_GameControllerRumble(
            controller,
            out_low,
            out_high,
            (out_low || out_high) ? out_duration : 0);
    }
}

// Route SDL GameController rumble calls from input.cpp through the independent
// gameplay mixer above. The real SDL function inside dispatch() was parsed
// before this macro is defined, so it remains the final hardware call.
#define SDL_GameControllerRumble(controller, low, high, duration) \
    gamepad_rumble::dispatch((controller), (low), (high), (duration))
