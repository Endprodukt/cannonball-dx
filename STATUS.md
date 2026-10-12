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

## Known bugs and limitations

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
- Touch: steering is digital (left/right buttons). No analog slider or tilt steering.
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
