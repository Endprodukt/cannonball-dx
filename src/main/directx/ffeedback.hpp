/***************************************************************************
    CannonBall DX Force Feedback Interface

    The gameplay code talks only to this small backend-neutral interface.
    On the test/sdl-wheel-ffb branch, Windows wheel FFB is implemented with
    SDL2 Haptics using SDL_HAPTIC_STEERING_AXIS and signed torque, following
    the same output model used by Flycast. Linux keeps the existing evdev
    fallback.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include <source_location>

namespace forcefeedback
{
    extern bool init(int max_force, int min_force, int force_duration);
    extern void close();
    extern int  set(
        int xdirection,
        int force,
        const std::source_location& source = std::source_location::current());
    extern bool is_supported();
    extern void set_centering_strength(
        int percent,
        const std::source_location& source = std::source_location::current());
    extern void set_tyre_slip(
        bool active,
        const std::source_location& source = std::source_location::current());
    extern void set_enabled(bool enabled);
    extern void set_gain(int percent);
    extern void stop();
};
