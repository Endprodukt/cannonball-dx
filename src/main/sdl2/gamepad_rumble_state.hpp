#pragma once

#include <algorithm>
#include <SDL.h>

#include "main.hpp"
#include "frontend/config.hpp"
#include "engine/oinputs.hpp"
#include "engine/omusic.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"

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

    // Central output wrapper for SDL GameController rumble. The normal
    // CannonBall rumble request is used as the baseline, then short contextual
    // effects may override it. This keeps the user's RUMBLE STRENGTH setting as
    // the master level for every effect.
    inline int dispatch(
        SDL_GameController* controller,
        Uint16 low_frequency,
        Uint16 high_frequency,
        Uint32 duration_ms)
    {
        using namespace detail;

        // Calls in menus are also used for capability probing and explicit
        // stop requests. Never turn those into gameplay effects.
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
        // Music selection: a light high-frequency click when moving to
        // another song position.
        // ------------------------------------------------------------
        if (outrun.game_state == GS_MUSIC)
        {
            const int selection = omusic.get_music_selected();

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
        // Gear change: one short mechanical bump. GAMEPAD has no notion
        // of left/right force, so use the low motor for the hit and the
        // high motor for a brief rebound.
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
        // Start-line rev effect: while the Ferrari is parked and waiting
        // for the start, throttle progressively builds a light engine buzz.
        // ------------------------------------------------------------
        const bool waiting_for_start =
            outrun.game_state == GS_START2 ||
            outrun.game_state == GS_START3 ||
            (outrun.game_state == GS_START1 &&
             oferrari.state == OFerrari::FERRARI_LOGIC);

        int start_target = 0;

        if (waiting_for_start && master > 0)
        {
            int pedal = oinputs.acc_adjust;
            pedal = std::clamp(pedal, 0, 0xFF);

            // Ignore tiny pedal noise. Full throttle reaches 55% of the
            // configured rumble master, leaving crashes clearly stronger.
            if (pedal >= 12)
            {
                start_target =
                    (static_cast<int>(master) * pedal * 55) /
                    (0xFF * 100);
            }
        }

        // Smooth build-up, quicker release. At 30 game ticks per second a
        // stomp to full throttle takes roughly a third of a second to peak.
        const int ramp_up = std::max(1, static_cast<int>(master) / 16);
        const int ramp_down = std::max(1, static_cast<int>(master) / 10);

        if (start_level < start_target)
            start_level = std::min(start_target, start_level + ramp_up);
        else if (start_level > start_target)
            start_level = std::max(start_target, start_level - ramp_down);

        // ------------------------------------------------------------
        // Crash detection. Scenery crashes use crash_counter/state. Traffic
        // contacts use skid_counter, which is specifically set by vehicle
        // collisions. Give the initial impact and flip landing full master.
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

        // Start with the normal CannonBall request. Context effects below are
        // ordered by priority: crash > traffic hit > shift > music > start rev.
        Uint16 out_low = low_frequency;
        Uint16 out_high = high_frequency;
        Uint32 out_duration = duration_ms;
        bool custom_effect = false;

        if (master > 0 && ocrash.crash_counter > 0)
        {
            const Uint32 crash_age = now - crash_started;

            if (crash_age < 150)
            {
                // Initial collision: hard, unmistakable impact.
                out_low = master;
                out_high = master;
            }
            else if (ocrash.crash_state >= 1 && ocrash.crash_state <= 4)
            {
                // Spin / flip / slide: chunky alternating vibration rather
                // than one flat continuous buzz.
                const bool heavy_phase = ((now / 90U) & 1U) == 0;
                out_low = scaled(master, heavy_phase ? 85 : 55);
                out_high = scaled(master, heavy_phase ? 70 : 35);
            }
            else if (landing_started && now - landing_started < 140)
            {
                // Flip landing gets another full hit.
                out_low = master;
                out_high = master;
            }
            else
            {
                out_low = scaled(master, 55);
                out_high = scaled(master, 35);
            }

            custom_effect = true;
        }
        else if (master > 0 &&
                 traffic_hit_started &&
                 now - traffic_hit_started < 130)
        {
            out_low = scaled(master, 85);
            out_high = scaled(master, 65);
            custom_effect = true;
        }
        else if (master > 0 && shift_started && now - shift_started < 85)
        {
            const Uint32 age = now - shift_started;

            if (age < 45)
            {
                out_low = scaled(master, 50);
                out_high = scaled(master, 20);
            }
            else
            {
                out_low = scaled(master, 20);
                out_high = scaled(master, 35);
            }

            custom_effect = true;
        }
        else if (master > 0 &&
                 music_pulse_started &&
                 now - music_pulse_started < 75)
        {
            out_low = 0;
            out_high = scaled(master, 28);
            custom_effect = true;
        }
        else if (master > 0 && waiting_for_start && start_level > 0)
        {
            out_low = static_cast<Uint16>(start_level / 4);
            out_high = static_cast<Uint16>(start_level);
            custom_effect = true;
        }

        if (custom_effect)
            out_duration = 45;

        return SDL_GameControllerRumble(
            controller,
            out_low,
            out_high,
            out_duration);
    }
}

// input.cpp includes this header before issuing the SDL rumble calls. Route
// those calls through the dispatcher above so gameplay effects can be layered
// over the original CannonBall rumble without touching wheel force feedback.
#define SDL_GameControllerRumble(controller, low, high, duration) \
    gamepad_rumble::dispatch((controller), (low), (high), (duration))
