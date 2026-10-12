# CannonBall DX - Status

Repository-only working notes. Not shipped in release packages.

## Current state

- **Windows**: the tested platform. Builds with Visual Studio 2022 + vcpkg.
- **Linux**: builds in CI, not tested at runtime.
- **Android (arm64-v8a)**: new. Project in `android/`, built by
  `.github/workflows/android-build.yml`. The APK builds locally (2026-10-12) and is
  signed and packaged correctly. **Not yet run on a device** - see "Outstanding".
- **Touch controls**: implemented in `src/main/sdl2/touchcontrols.*`, compiled
  **only for Android** (`__ANDROID__` guards; the Windows/Linux binaries contain
  none of it). The logic was verified once in a temporary Windows build with
  injected finger events: menu navigation, settings page, layout editor (drag,
  size buttons, pinch, hide/show, page switch, done/save), persistence in
  `config.xml`, and driving input in game.

## Needs verification on a device

Built but not yet seen running on the phone (2026-10-12):

- 20-9 aspect mode (21:9 engine frame, centre cropped; `video.aspect_20_9`).
- CRT shader in highp on Android (fix for the broken shadow mask / noise caused by
  16-bit mediump floats on mobile GPUs). The overlay-based mask and the Blargg
  filter itself were not changed.
- Touch vibration and phone rumble (`touch.haptics`, `touch.rumble`): build, never
  felt on a device. Rumble strength follows `controls.rumble`.
- Tilt steering (`touch.tilt`, `touch.tilt_range`): builds, never run on a device.
  The steering direction is derived from geometry, not yet confirmed by hand.

## Known bugs and limitations

- Android 20-9: the CRT shape overlay and screen curvature are computed for the
  full 21:9 frame, so their left and right edges are cropped with the picture.
- Android: CONFIG INPUTS still shows the WHEEL column of the binding matrix.

- CI: the Windows and Linux workflows fail on pull requests at the artifact
  upload step, because `github.ref_name` (`55/merge`) contains a slash. The builds
  themselves succeed. Pre-existing; left untouched because the Windows build
  setup is not to be changed without being asked.

- Android: only `arm64-v8a` is built. Other ABIs can be requested with
  `./gradlew assembleRelease -PabiFilters=arm64-v8a,x86_64` but are untested.
- Android: APKs are signed with the public test key in
  `android/cannonball-dx-test.keystore`. Fine for test builds, not for a store release.
- Android: wheel force feedback is not available (stub backend). Controller rumble
  goes through SDL.
- Android: in-menu `DISPLAY MODE` and `DISPLAY DEVICE` are removed; the surface is
  always fullscreen.
- Touch: steering is digital buttons or accelerometer tilt. No analog slider.
- Scanlines (all platforms, inherited): at 2X-4X engine resolution there is one
  scanline per original line (224); at original resolution only every other row
  can be dimmed (112). On displays whose height is not a multiple of 224 the
  lines land on fractional pixels and look fine/uneven. Not changed.
- Touch: the frontend layout has no VIEW button, so the Time Trial course selector
  cannot toggle traffic by touch (the Menu Accept button starts the run).
- Touch: the layout editor's toolbar sits in the middle of the screen; a button
  dragged underneath it has to be grabbed by its edge.

## Outstanding

1. Confirm the Android workflow is green in CI (first run failed on a clang-only
   error in `ohud.cpp`, fixed since).
2. First run on a real phone: ROM import, performance at the default 2X engine
   resolution, audio latency, pause/resume, controller hot-plug, notch handling.
3. Decide on release signing (private key via `CB_KEYSTORE_*` secrets).
4. Optional: analog/tilt steering for touch, more ABIs, a VIEW button for the
   Time Trial selector.
