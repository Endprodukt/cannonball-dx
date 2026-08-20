# CannonBall-SE

*A fork of Chris White's incredible OutRun engine, CannonBall, with enhancements by James Pearce and additional input, display and force-feedback work in this fork.*

CannonBall-SE is designed with home-made cabinets in mind and gives OutRun enthusiasts an improved experience on modern displays with minimal hardware requirements - anything from a Raspberry Pi Zero to a modern Windows PC.

This repository is based on **CannonBall-SE by James Pearce (J1mbo)**, which itself is based on **CannonBall by Chris White**. The aim of this fork is to preserve the work and features of both projects while extending controller, multi-device, ultrawide and steering-wheel / force-feedback support.

> **Note:** Official CannonBall-SE Windows releases are available from the upstream CannonBall-SE project: https://github.com/J1mbo/cannonball-se/releases

![CannonBall-SE Start Line](screenshots/CannonBall-SE-Startline.jpg)

---

## Overview

CannonBall-SE tries to create what feels like an authentic arcade-cab experience on modern hardware.

The SE project adds a fully configurable two-stage video-processing system with effects including NTSC filtering, screen curvature, shadow-mask emulation, analogue noise, vignette and desaturation. It also contains substantial performance, audio and cabinet-oriented improvements over the original CannonBall project.

This fork keeps those features and extends the input, display and existing force-feedback systems, aimed especially at PC racing cabinets where the steering wheel, pedals, shifter and buttons may be separate USB devices.

### Original CannonBall features

The underlying game engine is **Chris White's CannonBall**, a C++ recreation / enhancement of the Sega OutRun engine. CannonBall provides the core OutRun game engine, ROM loading, enhanced game modes, controls, widescreen support, wheel force feedback / haptic support and the large body of fixes and enhancements on which CannonBall-SE is built.

### CannonBall-SE features

CannonBall-SE by **James Pearce (J1mbo)** adds and improves, among other things:

- Configurable CRT / analogue-style video processing
  - NTSC filtering
  - CRT curvature
  - Shadow-mask effects
  - Analogue noise
  - Vignette and desaturation
  - Fast and Full shader modes
- High-resolution rendering improvements, including optional high-resolution sprite rendering
- Gameplay bug fixes
  - Random cars no longer appear near checkpoints momentarily
  - Wheel slip when cornering works consistently through stage transitions
  - Plus the fixes available in Chris White's original CannonBall
- Gameplay enhancements
  - Rumble when skidding
  - Automatic frame-rate control
  - Multi-threaded engine for better performance on low-power hardware
- Re-written audio module
  - Improved stability, especially on low-spec Raspberry Pi systems
  - Reduced audio stutter
  - Arcade-accurate playback speed
  - WAV, MP3 and YM custom music support
  - Configurable custom-music volume
- Machine statistics
  - Tracks games played
  - Tracks machine running hours
- Raspberry Pi watchdog support for cabinet installations
- Performance improvements and stability fixes
- RISC-V RVV SIMD path and SSE2 fallback support contributed by **rtissera**

### Additional features in this fork

This fork adds:

#### Display

- **21:9 ultrawide display support**
  - Adds a dedicated 21:9 presentation for ultrawide displays
  - Extends the original 4:3 presentation with an additional ultrawide display option

#### Multi-device controls

- **True multi-device input support**
  - Steering, accelerator and brake can come from different SDL devices
  - Useful for separate USB wheels, pedals, shifters and button interfaces
  - Devices are tracked by persistent signatures instead of relying only on SDL device order
- **Per-axis device bindings**
  - Wheel, accelerator, brake and motor-position inputs remember which physical device they belong to
  - Bindings are restored when CannonBall-SE starts again
- **Improved controller configuration**
  - Input configuration records both the axis/button and the device that generated it
  - Custom HAT bindings for menu directions are supported
  - Direction bindings can be stored independently

#### Force feedback

- **Reworked Windows force-feedback path using DirectInput 8**
  - Extends CannonBall's existing steering-wheel force-feedback / haptic system
  - Native constant-force steering effects
  - Independent centering spring
  - Periodic sine effect for tyre-slip / vibration feedback
  - Runtime enable / disable support
  - Reacquisition handling if the DirectInput device is temporarily lost
- **Adjustable Force Feedback Strength**
  - 10-100% in 10% steps
  - Available from the in-game Controls menu
- **Adjustable Centering Strength**
  - 0-100% in 10% steps
  - 0% disables the centering spring completely
- **Expanded driving and presentation FFB**
  - Speed- and corner-dependent steering weight
  - Tyre-slip vibration
  - Gear-change kick
  - Off-road vibration and outward pull
  - Different feedback profiles for bumps, spins, high-speed flips and landings
  - Start-sequence steering and throttle/rev feedback
  - Physical steering-wheel detents on the music-selection screen
- **Cleaner FFB configuration**
  - Old low-level `max_force`, `min_force` and `force_duration` XML options are internal tuning values and are removed from existing config files when saved

---

## Force Feedback Effects

CannonBall already included force-feedback / haptic support for steering wheels. The Windows work in this fork does **not** replace a rumble-only system; it reworks and expands CannonBall's existing wheel FFB using DirectInput 8.

The current `master` branch combines the original OutRun motor logic with additional effects that use game state, vehicle speed, road curvature, wheel state and presentation events.

### Normal cornering

The normal directional OutRun steering force remains present, but the centering spring now also changes dynamically while driving on-road.

- Steering weight increases smoothly with **vehicle speed**.
- Sharper bends receive more spring boost than gentle bends.
- The change is continuous rather than jumping between obvious strength bands.
- The dynamic cornering boost is disabled during crashes, skids and off-road states so separate effects do not simply stack on top of each other.
- The user's configured `centering_strength` remains the base value, so lowering the spring also lowers the extra cornering weight.

This is intended to make fast corners feel heavier without turning normal straight-line driving into an excessively stiff centering spring.

### Gear-change kick

Changing between low and high gear produces a very short mechanical kick through the wheel.

- The initial kick is strong and deliberately brief.
- A smaller rebound follows immediately afterwards.
- The direction changes depending on whether the car is shifted into high or low gear.
- The effect runs on top of the normal in-game steering feedback and ends automatically after the short transient.

### Tyre slip / on-road skid vibration

When the tyres are visibly slipping on the road, the Windows DirectInput path runs a periodic sine effect through the steering axis.

- The vibration uses a relatively high-frequency sine wave rather than repeatedly replacing the main steering force with simple left/right commands.
- The centering load is temporarily reduced while the tyres are slipping, making the steering feel slightly looser.
- As soon as grip returns, the previous configured / dynamic centering strength is restored.

This is separate from the stronger directional feedback used for collision-related spins.

### Off-road feedback

Off-road driving combines the original alternating motor pattern with a directional pull that tries to drag the steering wheel farther away from the road.

- With **one side of the car off-road**, the vibration component is deliberately restrained to roughly 50% so the outward pull remains easy to feel.
- When the car is **fully off-road**, the vibration returns to full amplitude while the constant outward bias is reduced. This leaves enough range for the alternating rough-surface effect instead of having the pull mask it.
- The pull direction follows the side of the road the car left.

The result is meant to distinguish "one wheel in the dirt" from "the whole car is off the road" instead of treating both states as the same rumble pattern.

### Crash feedback

Crash feedback is now state-aware rather than using one generic vibration for every accident.

- **Low-speed scenery bump**
  - One blunt impact
  - Followed by a small rebound
- **Medium-speed spin / collision**
  - Strong initial contact
  - Followed by sustained alternating steering yanks while the car rotates or slides
  - Centering is reduced so the wheel feels less planted than during normal driving
- **High-speed flip**
  - Very strong initial impact
  - Alternating lateral loads through the pre-flip spin
  - Continued steering load while the Ferrari is airborne, reversing with the flip animation
  - Steering becomes much lighter in the air
  - A separate landing impact and rebound is generated when the car comes back down

The crash effects deliberately alter both constant-force direction and steering weight so a bump, spin and full airborne crash do not all feel identical.

### Start-sequence feedback

Force feedback is also active before normal gameplay begins.

- During the Ferrari's animated drive into the starting position, a restrained steering load follows the car's movement toward the centre of the screen.
- Once the Ferrari is sitting on the grid, pressing the accelerator generates a light engine / rev vibration through the wheel.
- Rev vibration strength follows the actual accelerator value.
- A short internal ramp prevents a fully pressed pedal from producing an instant full-strength buzz.
- The normal driving FFB takes over when the race begins.

### Music-selection wheel detents

The music-selection screen now uses the steering wheel itself as a physical selector.

Each available track occupies a virtual position across the wheel's steering range. Force feedback creates a mechanical detent at the selected track position, making the radio selector feel more like a real switch than a free-moving analog axis.

- With the original three songs, the familiar left / centre / right positions are retained naturally.
- If custom music adds more tracks, additional selector positions are distributed evenly across the available steering range.
- A small hysteresis prevents steering noise from rapidly flickering between two tracks at a boundary.
- The selector uses a combination of the centering spring and constant force to pull the wheel toward the currently selected song position.
- As more tracks are added and the virtual positions move closer together, the selector spring is strengthened progressively so individual detents remain distinguishable.
- The selector spring is capped internally so a large playlist cannot make the wheel excessively heavy.
- Leaving the music-selection screen restores the normal user-configured FFB gain and centering strength.

This effect works with the existing CannonBall-SE custom-music system, so added WAV/MP3/YM tracks can also receive their own physical selector positions.

### FFB configuration philosophy

The individual effects above are currently **automatic game-state effects** rather than separate XML sliders for every event.

The two main user-facing controls remain:

```xml
<haptic enabled="1">
    <strength>50</strength>
    <centering_strength>30</centering_strength>
</haptic>
```

- `strength` controls the master level used by dynamic / transient FFB effects.
- `centering_strength` controls the base centering spring and also acts as the starting point for dynamic cornering weight.
- Both settings can be changed from the Controls menu without manually editing XML.
- Setting centering to `0` disables the normal centering spring while leaving dynamic FFB available.

The former experimental `max_force`, `min_force` and `force_duration` values are **not user-facing settings anymore**. The current code uses internal values for those parameters and removes legacy entries from the XML when the configuration is saved.

### Selecting a specific Windows FFB device

If multiple DirectInput force-feedback devices are connected, the Windows backend can optionally target a specific VID/PID through the `FF_TARGET_VIDPID` environment variable.

Example:

```text
FF_TARGET_VIDPID=0x046d:0xc24f
```

If no target is specified, the first suitable force-feedback device is used.

---

## Supported Platforms

- **All Raspberry Pi boards** running Raspberry Pi OS
- **x86/x64 PCs** (Intel/AMD) running Ubuntu or Windows 11

For Linux, a desktop is not required - the command-line version of the OS can be used. Compiling is automated on both platforms.

The reworked DirectInput steering-wheel force-feedback path and the newer DirectInput-specific effects documented above are primarily intended for **Windows**. Linux retains the existing evdev-based force-feedback path.

---

## ROMs (Required)

CannonBall-SE requires a copy of the original **OutRun revision B** ROM set. Copy the ROMs into the project's `roms/` directory. You are expected to legally own the original ROMs; usage may be restricted by local law.

---

## Quick Start Guide (Linux)

Getting going on Raspberry Pi systems or Ubuntu is straightforward - run the included `install.sh`. It installs prerequisites, builds the project and sets device permissions automatically.

```bash
# 1) Fetch sources
git clone https://github.com/Endprodukt/cannonball-se.git
cd cannonball-se

# 2) Build & set up
chmod +x install.sh
./install.sh

# 3) Add your OutRun rev B ROMs
mkdir -p roms
# copy your ROM set into ./roms

# 4) Reboot, then run
build/cannonball-se

# If running under a desktop, Wayland may improve frame-rate:
# SDL_VIDEODRIVER=wayland build/cannonball-se
```

The script installs system packages, compiles the game with CMake into `./build/`, and applies permissions needed for features such as `/dev/watchdog` and Linux haptics.

---

## Quick Start Guide (Windows)

CannonBall-SE can be compiled with Visual Studio. See:

`docs/Compiling-On-Windows.txt`

The features documented here are included in the **`master`** branch.

---

## Configuring Controls and Multiple Devices

The easiest and recommended way to configure controls is through:

**Menu -> Settings -> Controls**

When redefining controls, CannonBall-SE now remembers **which physical SDL device** supplied each analog axis. This means, for example, you can use:

- Steering axis from a wheel
- Accelerator and brake from a separate USB pedal set
- Buttons or gears from another controller / USB interface

The saved configuration contains the normal axis number plus an `axis_device` entry containing a persistent device signature. A typical generated section can look like this:

```xml
<controls>
    <analog enabled="1">
        <axis>
            <wheel>0</wheel>
            <accel invert="1">2</accel>
            <brake invert="1">1</brake>
            <motor>-1</motor>
        </axis>

        <axis_device>
            <wheel>DEVICE-GUID|A3|B12|H1</wheel>
            <accel>DEVICE-GUID|A3|B12|H1</accel>
            <brake>OTHER-DEVICE-GUID|A3|B0|H0</brake>
            <motor></motor>
        </axis_device>

        <wheel>
            <zone>0</zone>
            <dead>0</dead>
        </wheel>

        <haptic enabled="1">
            <strength>50</strength>
            <centering_strength>30</centering_strength>
        </haptic>
    </analog>
</controls>
```

**Do not normally edit the device signature manually.** It is generated from the SDL device GUID plus its axis, button and HAT counts and is written automatically when controls are configured. On startup, CannonBall-SE scans the connected devices and restores each binding to the matching device.

If hardware is changed, or a saved device can no longer be found, simply redefine that control in the in-game menu.

### New / relevant control options

| XML option | Range / values | Description |
|---|---:|---|
| `controls.analog enabled` | `0`, `1`, `2` | Analog-control mode. Can also be changed in the Controls menu. |
| `controls.analog.axis.wheel` | SDL axis index | Steering axis on its bound device. |
| `controls.analog.axis.accel` | SDL axis index | Accelerator axis on its bound device. |
| `controls.analog.axis.brake` | SDL axis index | Brake axis on its bound device. |
| `controls.analog.axis.motor` | SDL axis index / `-1` | Optional motor-position input used by cabinet-related functionality. |
| `controls.analog.axis_device.*` | generated string | Persistent physical-device signature for each analog axis. |
| `invert="1"` | `0` / `1` | Reverses an accelerator or brake axis. |
| `controls.analog.wheel.zone` | integer | Adjusts how much of the physical steering range is used. |
| `controls.analog.wheel.dead` | integer | Steering dead zone around centre. |
| `controls.analog.haptic enabled` | `0` / `1` | Enables steering-wheel force feedback. |
| `controls.analog.haptic.strength` | `10`-`100` | Overall FFB strength in percent. Values are clamped to this range. |
| `controls.analog.haptic.centering_strength` | `0`-`100` | Native centering-spring strength in percent. `0` disables the normal spring. |
| `controls.rumble` | `0.0`-`1.0` | Basic controller rumble level. |

Custom HAT / directional bindings are also stored in the config when configured through the menu. These are primarily intended to make menu navigation work cleanly with devices whose D-pad is exposed by SDL as a HAT rather than normal buttons.

---

## Command-line Options (summary)

- `-h` - Show options and man-page link, then quit
- `-list-audio-devices` - List available playback devices, then quit
- `-cfgfile <path>` - Use a specific `config.xml`
- `-file <layout>` - Load LayOut Editor track data (custom routes)
- `-30` or `-60` - Force 30 or 60 fps (disables auto selection)
- `-t [1-4]` - Override hardware thread detection
- `-x` - Disable single-core Raspberry Pi board detection
- `-1` - Use single-core mode (plus sound thread)
- `-perftest` - Enable maximum frame-rate performance testing

---

## Default Controls (keyboard)

- **Start (free-play):** `S`
- **Coin-In:** `C`
- **Accelerate:** `A`
- **Brake:** `Z`
- **Low / High Gear:** `G` / `H`
- **Steer:** Left / Right arrows
- **Change View:** `V`
- **Menu:** `M`
- **Menu Up / Down:** Up / Down arrows
- **Select / Confirm:** `S`
- **Quit:** `Esc`

Controls for keyboards, wheels, pads and other SDL devices can be remapped in **Menu -> Settings -> Controls**.

Quit can be remapped to `F10` instead of `Esc` in **Menu -> Settings -> Master Break Key**.

---

## Wheels & Gamepads

USB steering wheels, joysticks and gamepads are supported.

The current input system is designed to avoid the old assumption that every analog control has to live on one joystick. Separate wheel and pedal USB devices can therefore be configured independently.

CannonBall already supported wheel force feedback. On Windows, this fork extends that support through the reworked DirectInput path, including dynamic steering weight, transient effects, tyre-slip vibration, music-selector detents, adjustable overall strength and an independent centering spring. Basic SDL/controller rumble remains available for suitable gamepads.

---

## Custom Music (WAV/MP3/YM)

Place audio files in `./res/` using the scheme:

```text
[01-99]_Track_Display_Name.[wav|mp3|ym]
# e.g. 04_AHA_Take_On_Me.mp3
```

Indexes **01-03** replace the built-in tracks (`01` = *Magical Sound Shower*); **04+** add entries to the radio list. For WAV files, use **44.1 kHz, 16-bit stereo** where possible.

On the current `master` branch, analog steering divides the wheel range into one selector zone per available track. The FFB music-selector detent follows those positions, including additional custom tracks.

---

## Board Selection & Performance Notes

- **Pi Zero 2 W** is a low-cost way to run the full experience with 1280x1024 displays.
- **Pi 3 Model A** is a compact option with onboard analogue audio.

| Board | 30 fps | 60 fps | Notes |
|---|---|---|---|
| 2014 Pi B+ | Requires CPU @ 1GHz, GPU @ 400MHz, Core @ 350MHz | Not supported | Game settings are automatically applied at startup. Board clocks must be set in `/boot/firmware/config.txt`. |
| Pi Zero (W) | As per 2014 Pi B+; CPU is 1GHz by default | Not supported | As above. |
| Pi 2 (v1.1) | Supported at stock clocks | Hi-res possible with GPU @ 450MHz and Fast shader | 32-bit ARMv7 CPU. |
| Pi 2 (v1.2) | Supported at stock clocks | Supported with CPU @ 1GHz and Fast shader | 64-bit ARMv8 CPU. |
| Pi 3 (all) | Supported at stock clocks | Supported at stock clocks with Fast shader | HDMI or analogue audio preferred on systems affected by USB audio drop-outs. |
| Pi Zero 2 W | Supported at stock clocks | Similar to Pi 2 v1.2 | HDMI audio preferred. |
| Pi 4 & 5 | Supported at stock clocks | Supported at stock clocks | |

For Windows, just about any PC capable of running Windows 11 should have ample CPU/GPU performance for CannonBall-SE.

---

## Audio Troubleshooting on Linux

- **USB audio on Pi 2/3/Zero 2 W:** Setting USB to full-speed can avoid drop-outs (`dwc_otg.speed=1` in `/boot/firmware/cmdline.txt`). Be aware this can affect some USB peripherals.
- **Callback rate:** The callback rate can be changed to 16 ms, which increases latency slightly but can reduce drop-outs on some systems (**Menu -> Settings -> Sound/Music -> Callback Rate**).

---

## Watchdog (Linux only)

On hardware with a watchdog, including Raspberry Pi boards, CannonBall-SE can integrate with it so the OS automatically reboots if the game hangs - useful for unattended arcade cabinets and aggressive overclocking setups.

---

## Project Lineage & Credits

This fork exists because of the work of the projects and contributors before it. The additions in this repository are extensions of that work, not a replacement for it.

- **Chris White** - creator of the original **CannonBall** engine and the core OutRun recreation on which everything here is based.
- **James Pearce (J1mbo)** - creator and maintainer of **CannonBall-SE**, including its cabinet focus, video / CRT processing, performance work, audio improvements and many SE-specific fixes and enhancements.
- **Shay Green (Blargg)** - `snes_ntsc` NTSC filter library.
- **rtissera** - RISC-V RVV 1.0 SIMD support and x86 SSE2 fallback work merged into this fork.
- **CannonBall and CannonBall-SE contributors** - fixes, ports, testing and improvements accumulated by both upstream projects.
- **Endprodukt fork** - multi-device controller support, 21:9 ultrawide support and the current Windows steering-wheel / force-feedback extensions documented above.

Upstream projects:

- CannonBall: https://github.com/djyt/cannonball
- CannonBall-SE: https://github.com/J1mbo/cannonball-se

---

## License

- **Upstream CannonBall license:** non-commercial use; modified redistributions must include full source; warranty disclaimer. See `license.txt` in the repository.
- **CannonBall-SE additional terms:** SE enhancements © 2020-2025 James Pearce; provided "as is"; not for sale / monetisation; preserve notices. See `CannonBall-SE-license.txt`.
- **Third-party notices:** includes Blargg's `snes_ntsc` under **LGPL-2.1**. See `THIRD-PARTY-NOTICES.md` and `licenses/`.

*OutRun is a trademark of SEGA Corporation. This project is not affiliated with SEGA.*

---

## See Also

- Man page: `docs/cannonball-se.6`
- Windows compiling guide: `docs/Compiling-On-Windows.txt`