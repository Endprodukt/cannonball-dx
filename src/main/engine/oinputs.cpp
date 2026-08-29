/***************************************************************************
    Process Inputs.
    
    - Read & Process inputs and controls.
    - Note, this class does not contain platform specific code.
    
    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <iostream>

// Keep the multiplayer include before the normal engine/config include chain on
// Windows so Winsock2 is established before any Windows networking headers.
#include "engine/multiplayer.hpp"
#include "main.hpp"
#include "engine/car_palette_state.hpp"
#include "engine/ocrash.hpp"
#include "engine/oinputs.hpp"
#include "engine/ostats.hpp"

// Main loop pause flag. Multiplayer uses the existing engine pause gate only
// while GS_INIT_GAME is waiting for both players, so rendering/input/networking
// continue while the actual OutRun engine is held before race initialization.
extern bool pause_engine;

namespace
{
    bool multiplayer_colour_left_down = false;
    bool multiplayer_colour_right_down = false;
    bool multiplayer_colour_gear1_down = false;
    bool multiplayer_colour_gear2_down = false;

    int wrap_multiplayer_colour(int colour)
    {
        while (colour < 0)
            colour += car_palette_state::COLOR_COUNT;
        while (colour >= car_palette_state::COLOR_COUNT)
            colour -= car_palette_state::COLOR_COUNT;
        return colour;
    }

    void reset_multiplayer_colour_edges()
    {
        multiplayer_colour_left_down = false;
        multiplayer_colour_right_down = false;
        multiplayer_colour_gear1_down = false;
        multiplayer_colour_gear2_down = false;
    }

    // Player 2's waiting screen is not a normal CannonBall menu state. Capture
    // the raw control state BEFORE multiplayer.network_tick(), then apply it
    // immediately afterwards. This avoids has_pressed() ordering differences
    // between keyboard, D-pad and wheel-button backends. If the legacy network
    // handler already changed the colour, simply adopt that result instead of
    // stepping twice.
    void handle_multiplayer_colour_after_network(
        bool left_now,
        bool right_now,
        bool gear1_now,
        bool gear2_now,
        int colour_before_network)
    {
        if (!multiplayer.keep_lobby_color())
        {
            reset_multiplayer_colour_edges();
            return;
        }

        const int before = wrap_multiplayer_colour(colour_before_network);
        int current = wrap_multiplayer_colour(config.engine.car_pal);

        int direction = 0;
        if (left_now && !multiplayer_colour_left_down)
            direction = -1;
        else if (right_now && !multiplayer_colour_right_down)
            direction = 1;
        else if (gear1_now && !multiplayer_colour_gear1_down)
            direction = -1;
        else if (gear2_now && !multiplayer_colour_gear2_down)
            direction = 1;

        multiplayer_colour_left_down = left_now;
        multiplayer_colour_right_down = right_now;
        multiplayer_colour_gear1_down = gear1_now;
        multiplayer_colour_gear2_down = gear2_now;

        // The original multiplayer handler gets first chance during
        // network_tick(). If it already changed config.engine.car_pal, keep its
        // result. Otherwise apply the captured edge here deterministically.
        if (current == before && direction != 0)
        {
            current = wrap_multiplayer_colour(current + direction);
            config.engine.car_pal = current;
            osoundint.queue_sound(sound::BEEP1);
            std::cout << "[Multiplayer] Player 2 colour -> " << current << std::endl;
        }

        // Make the visual Ferrari follow the temporary race colour immediately.
        // This is intentionally not persisted as the normal Attract default.
        oferrari.ferrari_pal = car_palette_state::palette_source(current);

        // The waiting screen owns these controls. Consume them after both the
        // legacy and deterministic colour paths have had their chance so they
        // cannot steer the Attract car or toggle the normal gear state below.
        input.keys[Input::LEFT] = false;
        input.keys[Input::RIGHT] = false;
        input.keys[Input::GEAR1] = false;
        input.keys[Input::GEAR2] = false;
    }
}

OInputs oinputs;

OInputs::OInputs(void)
{
}

OInputs::~OInputs(void)
{
}

void OInputs::init()
{
    // DX no longer exposes a global Analog Controls mode. Keep low-level axis
    // event processing enabled; each steering/pedal target decides at runtime
    // whether its active binding is analog or digital.
    input.analog = 1;

    input_steering  = STEERING_CENTRE;
    steering_old    = STEERING_CENTRE;
    steering_adjust = 0;
    acc_adjust      = 0;
    brake_adjust    = 0;
    steering_change = 0;

    steering_inc = config.controls.steer_speed;
    acc_inc      = config.controls.pedal_speed * 4;
    brake_inc    = config.controls.pedal_speed * 4;

    input_acc   = 0;
    input_brake = 0;
    gear        = false;
    crash_input = 0;
    delay1      = 0;
    delay2      = 0;
    delay3      = 0;
    coin1       = false;
    coin2       = false;
}

void OInputs::tick()
{
    // Capture Player 2 colour controls before network_tick can consume any
    // has_pressed() edge or clear a target key.
    const bool multiplayer_left_now = input.is_pressed(Input::LEFT);
    const bool multiplayer_right_now = input.is_pressed(Input::RIGHT);
    const bool multiplayer_gear1_now = input.is_pressed(Input::GEAR1);
    const bool multiplayer_gear2_now = input.is_pressed(Input::GEAR2);
    const int multiplayer_colour_before = config.engine.car_pal;

    // Multiplayer networking must continue in Attract, frontend/menu and Music
    // Select, not only while the Ferrari gameplay routine happens to be active.
    multiplayer.network_tick();

    handle_multiplayer_colour_after_network(
        multiplayer_left_now,
        multiplayer_right_now,
        multiplayer_gear1_now,
        multiplayer_gear2_now,
        multiplayer_colour_before);

    // START from Music Select changes the engine to GS_INIT_GAME. Freeze the
    // normal OutRun engine at that exact boundary until master/slave are both
    // ready and the shared launch deadline has elapsed. Input and rendering
    // continue because only the existing engine pause flag is asserted.
    if (cannonball::state == cannonball::STATE_GAME &&
        outrun.game_state == GS_INIT_GAME &&
        multiplayer.enabled())
    {
        pause_engine = multiplayer.hold_game_start();
    }

    // The old global Analog Controls switch is intentionally ignored in DX.
    // Explicit matrix bindings already tell us whether a target is an axis or
    // a button/HAT, while keyboard bindings are inherently digital.
    input.analog = 1;

    auto has_active_axis = [&](int target, int legacy_slot)
    {
        bool target_has_matrix_binding = false;

        const SDL_JoystickID gamepad_device = input.get_gamepad_device();
        const std::string gamepad_signature =
            input.get_device_signature(gamepad_device);

        for (const auto& binding : config.controls.device_bindings)
        {
            if (binding.target != target)
                continue;

            // Once a target exists in the DX matrix, its matrix binding type
            // owns that target. This prevents an old legacy axis from leaking
            // through after the user deliberately rebinds e.g. GAS to a button.
            target_has_matrix_binding = true;

            if (binding.type != device_binding_t::TYPE_AXIS ||
                binding.device.size() < 3 ||
                binding.device[1] != ':')
            {
                continue;
            }

            const std::string signature = binding.device.substr(2);
            if (signature.empty() || signature == "*")
                continue;

            if (binding.device[0] == 'G')
            {
                if (!gamepad_signature.empty() &&
                    signature == gamepad_signature)
                {
                    return true;
                }
            }
            else if (binding.device[0] == 'W')
            {
                for (const auto& device : input.get_devices())
                {
                    if (device.instance_id == gamepad_device)
                        continue;

                    if (input.get_device_signature(device.instance_id) == signature)
                        return true;
                }
            }
        }

        if (target_has_matrix_binding)
            return false;

        // Compatibility fallback for first-run/legacy configs before their
        // old axis slots are materialized into the DX binding matrix.
        if (legacy_slot < 0 || config.controls.axis[legacy_slot] < 0)
            return false;

        const std::string& legacy_signature =
            config.controls.axis_device[legacy_slot];

        if (legacy_signature.empty())
            return !input.get_devices().empty();

        for (const auto& device : input.get_devices())
        {
            if (input.get_device_signature(device.instance_id) == legacy_signature)
                return true;
        }

        return false;
    };

    const bool steering_axis =
        has_active_axis(device_binding_t::TARGET_STEER, 0);
    const bool accel_axis =
        has_active_axis(device_binding_t::TARGET_ACCEL, 1);
    const bool brake_axis =
        has_active_axis(device_binding_t::TARGET_BRAKE, 2);

    // Steering: an active axis is used directly. Keyboard/D-pad/button input
    // always overrides it while pressed and uses DIGITAL STEER SPEED. Without
    // an active axis the steering path is purely digital and recentres normally.
    if (steering_axis &&
        !input.is_pressed(Input::LEFT) &&
        !input.is_pressed(Input::RIGHT))
    {
        input_steering = input.a_wheel;
    }
    else
    {
        digital_steering();
    }

    auto update_pedal = [&](bool axis_active,
                            Input::presses digital_input,
                            int analog_value,
                            int& output,
                            int increment)
    {
        if (input.is_pressed(digital_input))
        {
            output += increment;
            if (output > 0xFF)
                output = 0xFF;
        }
        else if (axis_active)
        {
            output = analog_value;
        }
        else
        {
            output -= increment;
            if (output < 0)
                output = 0;
        }
    };

    // Gas and brake are decided independently, so every useful mixed setup is
    // valid: analog wheel + buttons, wheel + separate USB pedals, gamepad stick
    // + triggers, keyboard + one analog pedal, etc.
    update_pedal(
        accel_axis,
        Input::ACCEL,
        input.a_accel,
        input_acc,
        acc_inc);

    update_pedal(
        brake_axis,
        Input::BRAKE,
        input.a_brake,
        input_brake,
        brake_inc);
}
// DIGITAL CONTROLS: Digital Simulation of analog steering
void OInputs::digital_steering()
{
    // ------------------------------------------------------------------------
    // STEERING
    // ------------------------------------------------------------------------
    if (input.is_pressed(Input::LEFT))
    {
        // Recentre wheel immediately if facing other way
        if (input_steering > STEERING_CENTRE) input_steering = STEERING_CENTRE;

        input_steering -= steering_inc;
        if (input_steering < STEERING_MIN) input_steering = STEERING_MIN;
    }
    else if (input.is_pressed(Input::RIGHT))
    {
        // Recentre wheel immediately if facing other way
        if (input_steering < STEERING_CENTRE) input_steering = STEERING_CENTRE;

        input_steering += steering_inc;
        if (input_steering > STEERING_MAX) input_steering = STEERING_MAX;
    }
    // Return steering to centre if nothing pressed
    else
    {
        if (input_steering < STEERING_CENTRE)
        {
            input_steering += steering_inc;
            if (input_steering > STEERING_CENTRE)
                input_steering = STEERING_CENTRE;
        }
        else if (input_steering > STEERING_CENTRE)
        {
            input_steering -= steering_inc;
            if (input_steering < STEERING_CENTRE)
                input_steering = STEERING_CENTRE;
        }
    }
}

// DIGITAL CONTROLS: Digital Simulation of analog pedals
void OInputs::digital_pedals()
{
    // ------------------------------------------------------------------------
    // ACCELERATION
    // ------------------------------------------------------------------------

    if (input.is_pressed(Input::ACCEL))
    {
        input_acc += acc_inc;
        if (input_acc > 0xFF) input_acc = 0xFF;
    }
    else
    {
        input_acc -= acc_inc;
        if (input_acc < 0) input_acc = 0;
    }

    // ------------------------------------------------------------------------
    // BRAKE
    // ------------------------------------------------------------------------

    if (input.is_pressed(Input::BRAKE))
    {
        input_brake += brake_inc;
        if (input_brake > 0xFF) input_brake = 0xFF;
    }
    else
    {
        input_brake -= brake_inc;
        if (input_brake < 0) input_brake = 0;
    }
}

void OInputs::do_gear()
{
    // ------------------------------------------------------------------------
    // GEAR SHIFT
    // ------------------------------------------------------------------------

    // Automatic Gears: Don't do anything
    if (config.controls.gear == config.controls.GEAR_AUTO)
        return;

    else
    {
        // Manual: Cabinet Shifter
        if (config.controls.gear == config.controls.GEAR_PRESS)
            gear = !input.is_pressed(Input::GEAR1);

        // Manual: Two Separate Buttons for gears
        else if (config.controls.gear == config.controls.GEAR_SEPARATE)
        {
            if (input.has_pressed(Input::GEAR1))
                gear = false;
            else if (input.has_pressed(Input::GEAR2))
                gear = true;
        }

        // Manual: Keyboard/Digital Button
        else
        {
            if (input.has_pressed(Input::GEAR1))
                gear = !gear;
        }
    }
}

// Adjust Analogue Inputs
//
// Read, Adjust & Write Analogue Inputs
// In the original, these values are set during a H-Blank routine
//
// Source: 74E2

void OInputs::adjust_inputs()
{
    // Cap Steering Value
    if (input_steering < STEERING_MIN) input_steering = STEERING_MIN;
    else if (input_steering > STEERING_MAX) input_steering = STEERING_MAX;

    if (crash_input)
    {
        crash_input--;
        int16_t d0 = ((input_steering - 0x80) * 0x100) / 0x70;
        if (d0 > 0x7F) d0 = 0x7F;
        else if (d0 < -0x7F) d0 = -0x7F;
        steering_adjust = ocrash.crash_counter ? 0 : d0;
    }
    else
    {
        // no_crash1:
        int16_t d0 = input_steering - steering_old;
        steering_old = input_steering;
        steering_change += d0;
        d0 = steering_change < 0 ? -steering_change : steering_change;

        // Note the below line "if (d0 > 2)" causes a bug in the original game
        // whereby if you hold the wheel to the left whilst stationary, then accelerate the car will veer left even
        // when the wheel has been centered
        if (config.engine.fix_bugs || d0 > 2)
        {
            steering_change = 0;
            // Convert input steering value to internal value
            d0 = ((input_steering - 0x80) * 0x100) / 0x70;
            if (d0 > 0x7F) d0 = 0x7F;
            else if (d0 < -0x7F) d0 = -0x7F;
            steering_adjust = ocrash.crash_counter ? 0 : d0;
        }
    }

    // Cap & Adjust Acceleration Value
    int16_t acc = input_acc;
    if (acc > PEDAL_MAX) acc = PEDAL_MAX;
    else if (acc < PEDAL_MIN) acc = PEDAL_MIN;
    acc_adjust = ((acc - 0x30) * 0x100) / 0x61;

    // Cap & Adjust Brake Value
    int16_t brake = input_brake;
    if (brake > PEDAL_MAX) brake = PEDAL_MAX;
    else if (brake < PEDAL_MIN) brake = PEDAL_MIN;
    brake_adjust = ((brake - 0x30) * 0x100) / 0x61;
}

// Simplified version of do credits routine. 
// I have not ported the coin chute handling code, or dip switch routines.
//
// Returns: 0 (No Coin Inserted)
//          1 (Coin Chute 1 Used)
//          2 (Coin Chute 2 Used)
//          3 (Key Pressed / Service Button)
//
// Source: 0x6DE0
uint8_t OInputs::do_credits()
{
    if (input.has_pressed(Input::COIN))
    {
        if (!config.engine.freeplay && ostats.credits < 9)
        {
            ostats.credits++;
            // todo: Increment credits total for bookkeeping
            osoundint.queue_sound(sound::COIN_IN);
        }
        return 3;
    }
    else if (coin1)
    {
        coin1 = false;
        if (!config.engine.freeplay && ostats.credits < 9)
        {
            ostats.credits++;
            osoundint.queue_sound(sound::COIN_IN);
        }
        return 1;
    }
    else if (coin2)
    {
        coin2 = false;
        if (!config.engine.freeplay && ostats.credits < 9)
        {
            ostats.credits++;
            osoundint.queue_sound(sound::COIN_IN);
        }
        return 2;
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------
// Menu Selection Controls
// ------------------------------------------------------------------------------------------------

bool OInputs::is_analog_l()
{
    if (input_steering < STEERING_CENTRE - 0x10)
    {
        if (--delay1 < 0)
        {
            delay1 = DELAY_RESET;
            return true;
        }
    }
    else
        delay1 = DELAY_RESET;
    return false;
}

bool OInputs::is_analog_r()
{
    if (input_steering > STEERING_CENTRE + 0x10)
    {
        if (--delay2 < 0)
        {
            delay2 = DELAY_RESET;
            return true;
        }
    }
    else
        delay2 = DELAY_RESET;
    return false;
}

bool OInputs::is_analog_select()
{
    if (input_acc > 0x90)
    {
        if (--delay3 < 0)
        {
            delay3 = DELAY_RESET;
            return true;
        }
    }
    else
        delay3 = DELAY_RESET;
    return false;
}
