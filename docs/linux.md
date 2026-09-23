# CannonBall DX on Linux

Linux support is currently being validated on the `linux-parity` branch.

## Current status

- Ubuntu 24.04 x86_64 builds are verified automatically with GitHub Actions.
- The normal SDL2 + OpenGL ES renderer builds unchanged.
- ROM/ZIP loading, TinyXML2 configuration, MP3 playback and the DX game modes are part of the normal Linux build.
- The modern SDL2 Haptics wheel backend is enabled on Linux on this branch, including constant force, centering spring, tyre-slip/start sine effects and the RPM-linked engine vibration.
- Physical wheel behaviour still needs testing on real Linux hardware. A successful CI build proves compilation/linking, not driver-specific force-feedback behaviour.

## Build from source on Ubuntu/Debian

From the repository root:

```bash
chmod +x install.sh
./install.sh
```

The helper installs the required development packages, builds a Release binary and configures restricted controller/hidraw access through the `input` group and udev.

After the first run of the setup helper, log out and back in (or reboot) before testing wheel force feedback if your user was newly added to the `input` group.

Put your supported OutRun ROM data in:

```text
roms/
```

Then start from the repository root:

```bash
./build/cannonball-dx
```

## GitHub Actions test package

The Linux workflow produces an artifact named:

```text
cannonball-dx-linux-x86_64
```

Extract it, add your ROM data to its `roms/` directory, keep the supplied directory structure intact, and launch:

```bash
./run.sh
```

The launcher deliberately changes to the package directory first because CannonBall DX currently resolves resources and save/config paths relative to its working directory.

## Wheel / FFB notes

The Linux parity branch uses SDL2 Haptics for the modern wheel backend instead of the older reduced evdev-only implementation. Device access therefore depends on both the Linux wheel driver and SDL exposing the wheel as a haptic-capable joystick.

For the first hardware test, verify at least:

1. Steering, accelerator and brake binding.
2. Low/high-speed centering spring.
3. Normal left/right constant-force effects.
4. Off-road pull/rumble.
5. Tyre-slip sine effect.
6. Start-grid rev shake.
7. In-race RPM-linked engine vibration.
8. Gear-change and music-selection detents.
9. Crash/spin/flip effects.
10. FFB stops correctly in menus/replays and on exit.

A remaining parity detail is hot rebinding: the current input code explicitly refreshes the selected FFB device after changing the steering binding on Windows. Linux can initialize the correct bound device at startup, but live wheel rebinding still needs to be made platform-neutral and tested before this branch should be considered release-ready.

## Known build warnings

The Ubuntu CI build currently emits several pre-existing compiler/dependency warnings (for example from miniz and legacy rendering/game code). They do not prevent the Linux build from completing, but they should not be confused with Linux-port failures.
