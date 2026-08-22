# CannonBall DX

*An arcade- and racing-wheel-focused fork of CannonBall-SE, based on Chris White's CannonBall OutRun engine.*

**CannonBall DX** builds on **CannonBall-SE by James Pearce (J1mbo)**, which itself is based on **CannonBall by Chris White**. The aim of this fork is to make CannonBall especially well suited to modern racing wheels, multi-device PC setups and dedicated arcade cabinets while keeping the original OutRun feel intact.

The additional work in this fork was developed with the assistance of **ChatGPT by OpenAI, using GPT-5.6 Sol**.

> Official CannonBall-SE releases are available from the upstream project: https://github.com/J1mbo/cannonball-se/releases

![CannonBall DX Start Line](screenshots/CannonBall-SE-Startline.jpg)

---

## CannonBall DX Highlights

- **Arcade cabinet and racing-wheel focus** with modern USB wheel, pedal, shifter and gamepad support
- **True multi-device input** - steering, pedals, shifter and buttons can come from different physical devices
- **Unified control-binding matrix** with separate Keyboard, Gamepad and Wheel assignments
- **Expanded DirectInput force feedback** - cornering weight, tyre slip, off-road effects, gear-change kick, crashes/spins, start/rev effects and music-selector detents
- **Gamepad rumble support** with enable and strength controls on supported controllers
- **Direct View 1 / View 2 / View 3 controls** alongside the original single-button view cycling
- **21:9 ultrawide support** in addition to the original 4:3 presentation and existing widescreen modes
- **Enhanced Attract Mode** with automatic camera presentation changes and cabinet lamp effects
- **Game-mode selection from the Music Select screen** - Original, Continuous or Time Trial via the VIEW controls
- **Expanded Ferrari colours** - Red, Blue, Yellow, Green, Cyan, Black, White and Silver, with a persistent default colour and per-run Music Select choice
- **Continuous Mode traffic follows the normal OutRun difficulty and stage scaling**
- **Arcade lamp outputs** for START, BRAKE and VIEW lamps via **MAME network output**, **Windows MAMEOutput / MAMEHooker** and the existing **SmartyPi** output path

---

## Upstream Features

### CannonBall

**Chris White's CannonBall** provides the core C++ recreation and enhancement of Sega OutRun, including the game engine, ROM loading, enhanced modes, controls, widescreen support and steering-wheel force feedback.

### CannonBall-SE

**James Pearce's CannonBall-SE** adds a large range of cabinet, performance, audio and display improvements, including:

- CRT / analogue video processing and NTSC filtering
- High-resolution rendering improvements
- Gameplay fixes and enhancements
- Automatic 30/60 fps operation
- Multi-threaded rendering for low-power hardware
- WAV, MP3 and YM custom music support
- Machine play-count and runtime statistics
- Raspberry Pi watchdog support
- Numerous performance and stability improvements
- RISC-V RVV SIMD support and x86 SSE2 fallback contributed by **rtissera**

CannonBall already included steering-wheel force feedback. CannonBall DX **extends that existing system** rather than replacing a rumble-only implementation.

---

## Supported Platforms

- Raspberry Pi systems running Raspberry Pi OS
- x86/x64 PCs running Ubuntu
- Windows 11 PCs

The expanded DirectInput force-feedback implementation is primarily intended for **Windows**. Linux retains the existing evdev-based FFB path.

---

## ROMs

CannonBall DX requires the original **OutRun revision B** ROM set. Copy the ROMs into the `roms/` directory.

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

The repository and executable currently retain the existing `cannonball-se` technical names for compatibility.

---

## Quick Start - Windows

CannonBall DX can be compiled with Visual Studio.

See:

`docs/Compiling-On-Windows.txt`

The features documented here are included in the **`master`** branch.

---

## Controls and Multiple Devices

Controls are configured through:

**Menu -> Settings -> Controls**

The binding editor provides separate **Keyboard**, **Gamepad** and **Wheel** columns. Steering, accelerator, brake, shifter and buttons can be assigned independently, including devices such as separate USB pedals or shifters.

Device bindings store a persistent device signature so assignments survive changes in SDL device order.

### Force Feedback settings

| XML option | Values | Description |
|---|---:|---|
| `controls.analog.haptic enabled` | `0` / `1` | Enables steering-wheel force feedback |
| `controls.analog.haptic.strength` | `10`-`100` | Overall FFB strength in percent |
| `controls.analog.haptic.centering_strength` | `0`-`100` | Base centering spring strength |
| `controls.rumble` | `0.0`-`1.0` | Gamepad rumble level |

The individual driving effects are automatic.

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
- **Default Car Colour in Attract Mode:** `F10`
- **Menu:** `M`
- **Quit:** `Esc`

While the car is driving in Attract Mode, press **F10** to cycle the Ferrari colour. The selected colour is saved as the new default and is restored on future starts.

Controls can be remapped in the in-game menu.

---

## Music Select

The Music Select screen can now also be used to prepare the next run:

- **VIEW** cycles Original -> Continuous -> Time Trial
- **VIEW1 / VIEW2 / VIEW3** select those modes directly
- **LOW / HIGH gear** changes Ferrari colour
- **START** confirms the selection

Ferrari colours are **Red, Blue, Yellow, Green, Cyan, Black, White and Silver**. The Music Select colour applies **only to the next race** and does not replace the saved default. After the run, Attract Mode returns to the default colour selected with **F10**.

Custom music files can still be placed in `./res/` using:

```text
[01-99]_Track_Display_Name.[wav|mp3|ym]
```

Tracks `01-03` replace the original songs. Tracks `04+` add additional entries to the radio selector. On supported Windows wheels, the selector uses physical FFB detents across the steering range.

---

## Arcade Outputs

CannonBall DX exposes its lamp outputs through:

- **MAME network output**
- **Windows MAMEOutput messages / MAMEHooker**
- **SmartyPi**

Currently exposed lamps include **START**, **BRAKE**, **VIEW**, **VIEW1**, **VIEW2** and **VIEW3**.

See `EXTERNAL_OUTPUTS.md` for configuration details.

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

CannonBall DX builds directly on the work of the original projects and their contributors.

- **Chris White** - creator of the original **CannonBall** engine and core OutRun recreation
- **James Pearce (J1mbo)** - creator and maintainer of **CannonBall-SE**, including its cabinet, video, performance, audio and gameplay enhancements
- **Shay Green (Blargg)** - `snes_ntsc` NTSC filter library
- **rtissera** - RISC-V RVV 1.0 SIMD support and x86 SSE2 fallback
- **CannonBall and CannonBall-SE contributors** - fixes, ports, testing and improvements across both upstream projects
- **Endprodukt** - CannonBall DX fork, multi-device input, ultrawide, cabinet-output and modern wheel / feedback extensions
- **ChatGPT by OpenAI - GPT-5.6 Sol** - development assistance for the additional work in CannonBall DX

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
