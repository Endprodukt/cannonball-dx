# CannonBall DX - Resume notes

What a later session needs to continue efficiently. Repository-only.

## Source layout conventions

- Many files come in pairs: `foo_base.cpp` holds the preserved upstream/SE code and
  `foo.cpp` includes it under renamed symbols and layers the DX behaviour on top
  (`menu`, `config`, `input`, `ffeedback`, `video`). Add DX features in the wrapper;
  touch `_base` files only for platform guards.
- `Config` gets its DX accessors from the `CANNONBALL_DX_CONFIG_EXTENSIONS` macro in
  `frontend/config.hpp`. They read/write the XML tree directly (`cfg.get_int` /
  `cfg.put_int`), so new settings need no struct field or load/save code.
- Menus are `std::vector<std::string>` pages. New pages are added in the final
  `Menu` class in `frontend/menu.hpp` (`tick_menu()` override), following
  `menu_endless` / `menu_bugfixes` / `menu_touch`.
- Source files are checked out with CRLF (`core.autocrlf=true`); keep them that way.

## Android port

- `android/app/build.gradle` clones the pinned SDL2 release into `android/deps/` at
  configuration time, compiles SDL's Java activity from it and passes the same tree
  to CMake as `CB_SDL2_SOURCE_DIR`. Java glue and `libSDL2.so` must always come
  from the same SDL release.
- The root `CMakeLists.txt` has `if(ANDROID)` branches: the game becomes
  `libmain.so`; tinyxml2 and mpg123 are fetched with FetchContent; EGL/GLESv2 are
  NDK system libraries; OpenMP is linked statically.
- `__linux__` is defined on Android. Desktop-Linux-only code (watchdog, backtrace,
  udev, evdev force feedback) is guarded with `!defined(__ANDROID__)`;
  `main.cpp` uses `CB_DESKTOP_LINUX` for this.
- The engine uses relative paths. `main()` changes into the app's external files
  directory; `LauncherActivity` unpacks `res/` from the APK assets there and imports
  the ROM set with the system file picker before starting `GameActivity`
  (`SDLActivity`). `resolveDataDir()` in Java and `android_prepare_storage()` in
  `main.cpp` must agree.
- stdout/stderr go to `cannonball-dx.log` in that directory - ask for this file
  when debugging a device-only problem.
- Worker threads are capped at 3 on Android (as on Windows) so SDL events and the
  GL context stay on the main thread.
- No Android toolchain is installed on the development PC; the APK is only built
  by GitHub Actions.

## Touch controls

- `TouchControls` (`sdl2/touchcontrols.*`) receives `SDL_FINGER*` events from
  `process_events()` and sets the ordinary `Input` key states through
  `Input::set_touch_key()`. Engine and menus need no touch-specific code.
- Rendering: `needs_clear()` then `render()` are called from both
  `finalize_frame()` implementations (`rendersurface.cpp`,
  `pixelscaler_renderer.hpp`) just before `glb::present()`.
- Settings are stored under `<touch>` in `config.xml`; each button is
  `x,y,size,visible` in permille (position of the centre relative to the screen,
  diameter relative to the screen height).
- To test without a touchscreen, inject `SDL_TouchFingerEvent`s into
  `touchcontrols.handle_finger()`. Posting Win32 mouse messages to the SDL window
  with `SDL_MOUSE_TOUCH_EVENTS=1` was unreliable.
