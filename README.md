# CannonBall-SE

*A fork of Chris White's CannonBall, extended by James Pearce's CannonBall-SE and further enhanced in this repository.*

CannonBall-SE is an enhanced OutRun engine aimed at arcade cabinets and modern PCs while retaining support for low-power Raspberry Pi systems.

This repository is based on **CannonBall-SE by James Pearce (J1mbo)**, which itself is based on **CannonBall by Chris White**. The additional work in this fork focuses on modern racing-wheel support, multi-device controls, 21:9 ultrawide output and expanded force feedback.

The modifications in this fork were developed with the assistance of **ChatGPT by OpenAI, using GPT-5.6 Sol**.

> Official CannonBall-SE releases are available from the upstream project: https://github.com/J1mbo/cannonball-se/releases

![CannonBall-SE Start Line](screenshots/CannonBall-SE-Startline.jpg)

---

## Features

### Original CannonBall

**Chris White's CannonBall** provides the core C++ recreation and enhancement of Sega OutRun, including the game engine, ROM loading, enhanced modes, controls, widescreen support and steering-wheel force feedback / haptics.

### CannonBall-SE

**James Pearce's CannonBall-SE** adds a large range of cabinet, performance, audio and display improvements, including:

- Configurable CRT / analogue video processing
  - NTSC filtering
  - Curvature
  - Shadow mask
  - Analogue noise
  - Vignette and desaturation
- High-resolution rendering improvements
- Gameplay fixes and enhancements
- Skid rumble
- Automatic 30/60 fps operation
- Multi-threaded rendering for low-power hardware
- Reworked audio system with WAV, MP3 and YM custom music support
- Machine play-count and runtime statistics
- Raspberry Pi watchdog support
- Numerous performance and stability improvements
- RISC-V RVV SIMD support and x86 SSE2 fallback contributed by **rtissera**

### Additional features in this fork

- **21:9 ultrawide support** in addition to the original 4:3 presentation and existing widescreen modes
- **True multi-device input**
  - Wheel, accelerator and brake may come from different USB / SDL devices
  - Separate shifters and button interfaces are supported
  - Device bindings survive changes in SDL device order
- **Improved controller configuration**
  - Analog axes remember the physical device they belong to
  - Custom HAT / directional bindings are supported
- **Reworked Windows DirectInput force feedback**
  - Adjustable overall FFB strength
  - Adjustable centering spring
  - Speed- and corner-dependent steering weight
  - Tyre-slip vibration
  - Gear-change kick
  - Off-road vibration and directional pull
  - Distinct crash / spin / flip feedback
  - Start-sequence and throttle/rev feedback
  - Physical wheel detents on the music-selection screen

CannonBall already included steering-wheel force feedback. This fork **extends that existing system** rather than replacing a simple rumble-only implementation.

---

## Supported Platforms

- Raspberry Pi systems running Raspberry Pi OS
- x86/x64 PCs running Ubuntu
- Windows 11 PCs

The expanded DirectInput force-feedback implementation is primarily intended for **Windows**. Linux retains the existing evdev-based FFB path.

---

## ROMs

CannonBall-SE requires the original **OutRun revision B** ROM set. Copy the ROMs into the `roms/` directory.

You are expected to legally own the original ROMs; usage may be restricted by local law.

---

## Quick Start - Linux

```bash
git clone https://github.com/Endprodukt/cannonball-se.git
cd cannonball-se
chmod +x install.sh
./install.sh
```

Then copy the OutRun revision B ROMs into `./roms/` and run:

```bash
build/cannonball-se
```

The included installer handles the required packages, build process and relevant Linux device permissions.

---

## Quick Start - Windows

CannonBall-SE can be compiled with Visual Studio.

See:

`docs/Compiling-On-Windows.txt`

The features documented here are included in the **`master`** branch.

---

## Controls and Multiple Devices

Controls are best configured through:

**Menu -> Settings -> Controls**

Steering, accelerator and brake can each be assigned to different physical devices. CannonBall-SE stores both the axis number and a persistent device signature so the bindings can be restored on the next launch.

Example:

```xml
<controls>
    <analog enabled="1">
        <axis>
            <wheel>0</wheel>
            <accel invert="1">2</accel>
            <brake invert="1">1</brake>
        </axis>

        <axis_device>
            <wheel>DEVICE-GUID|A3|B12|H1</wheel>
            <accel>DEVICE-GUID|A3|B12|H1</accel>
            <brake>OTHER-DEVICE-GUID|A3|B0|H0</brake>
        </axis_device>

        <haptic enabled="1">
            <strength>50</strength>
            <centering_strength>30</centering_strength>
        </haptic>
    </analog>
</controls>
```

Device signatures are generated automatically and normally should not be edited manually.

### Relevant FFB settings

| XML option | Values | Description |
|---|---:|---|
| `controls.analog.haptic enabled` | `0` / `1` | Enables steering-wheel force feedback. |
| `controls.analog.haptic.strength` | `10`-`100` | Overall FFB strength in percent. |
| `controls.analog.haptic.centering_strength` | `0`-`100` | Base centering spring strength. `0` disables it. |
| `controls.rumble` | `0.0`-`1.0` | Basic controller rumble level. |

The individual driving effects are automatic and do not require separate configuration.

If multiple DirectInput FFB devices are connected on Windows, a specific device can optionally be selected with the `FF_TARGET_VIDPID` environment variable.

Example:

```text
FF_TARGET_VIDPID=0x046d:0xc24f
```

---

## Default Keyboard Controls

- **Start:** `S`
- **Coin:** `C`
- **Accelerate:** `A`
- **Brake:** `Z`
- **Low / High Gear:** `G` / `H`
- **Steer:** Left / Right arrows
- **Change View:** `V`
- **Menu:** `M`
- **Quit:** `Esc`

Controls can be remapped in the in-game menu.

---

## Custom Music

Place custom audio files in `./res/` using:

```text
[01-99]_Track_Display_Name.[wav|mp3|ym]
```

Example:

```text
04_AHA_Take_On_Me.mp3
```

Tracks `01-03` replace the original three songs. Tracks `04+` add additional entries to the radio selector.

With an analog steering wheel, the available tracks are distributed across the steering range. The FFB system creates physical selector positions for them on supported Windows wheels.

---

## Command-Line Options

- `-h` - Show options
- `-list-audio-devices` - List audio devices
- `-cfgfile <path>` - Use a specific config file
- `-file <layout>` - Load custom LayOut Editor track data
- `-30` / `-60` - Force frame rate
- `-t [1-4]` - Override hardware thread detection
- `-x` - Disable single-core Raspberry Pi detection
- `-1` - Use single-core mode
- `-perftest` - Maximum frame-rate performance test

---

## Credits

This fork builds directly on the work of the original projects and their contributors.

- **Chris White** - creator of the original **CannonBall** engine and core OutRun recreation
- **James Pearce (J1mbo)** - creator and maintainer of **CannonBall-SE**, including its cabinet, video, performance, audio and gameplay enhancements
- **Shay Green (Blargg)** - `snes_ntsc` NTSC filter library
- **rtissera** - RISC-V RVV 1.0 SIMD support and x86 SSE2 fallback
- **CannonBall and CannonBall-SE contributors** - fixes, ports, testing and improvements across both upstream projects
- **Endprodukt** - multi-device input, 21:9 support and the current steering-wheel / force-feedback extensions in this fork
- **ChatGPT by OpenAI - GPT-5.6 Sol** - development assistance for the additional work in this fork

Upstream projects:

- CannonBall: https://github.com/djyt/cannonball
- CannonBall-SE: https://github.com/J1mbo/cannonball-se

---

## License

- **Upstream CannonBall license:** non-commercial use; modified redistributions must include full source; warranty disclaimer. See `license.txt`.
- **CannonBall-SE additional terms:** SE enhancements © 2020-2025 James Pearce; provided "as is"; not for sale / monetisation; preserve notices. See `CannonBall-SE-license.txt`.
- **Third-party notices:** includes Blargg's `snes_ntsc` under **LGPL-2.1**. See `THIRD-PARTY-NOTICES.md` and `licenses/`.

*OutRun is a trademark of SEGA Corporation. This project is not affiliated with SEGA.*

---

## See Also

- Man page: `docs/cannonball-se.6`
- Windows compiling guide: `docs/Compiling-On-Windows.txt`
