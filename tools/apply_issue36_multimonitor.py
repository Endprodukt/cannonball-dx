from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


def insert_before(path, marker, addition):
    replace_once(path, marker, addition + marker)


def insert_after(path, marker, addition):
    replace_once(path, marker, marker + addition)


# Shared display enumeration / preference resolver. It deliberately does not
# mutate the saved preference when a monitor is temporarily unavailable.
display_utils = r'''#pragma once

#include <SDL.h>

#include <algorithm>
#include <string>
#include <vector>

namespace display_utils
{
    inline int count()
    {
        const int displays = SDL_GetNumVideoDisplays();
        return displays > 0 ? displays : 0;
    }

    inline std::string name(int index)
    {
        const int displays = count();
        if (index < 0 || index >= displays)
            return {};

        const char* value = SDL_GetDisplayName(index);
        return value ? std::string(value) : std::string();
    }

    // Display indices are not stable across reconnects. Prefer a unique saved
    // name; for duplicate names use the saved index to disambiguate; then fall
    // back to any still-valid saved index and finally display 0.
    inline int resolve_preferred(int stored_index, const std::string& stored_name)
    {
        const int displays = count();
        if (displays <= 0)
            return 0;

        if (!stored_name.empty())
        {
            std::vector<int> matches;
            for (int i = 0; i < displays; ++i)
            {
                if (name(i) == stored_name)
                    matches.push_back(i);
            }

            if (matches.size() == 1)
                return matches.front();

            if (matches.size() > 1 &&
                std::find(matches.begin(), matches.end(), stored_index) != matches.end())
            {
                return stored_index;
            }
        }

        if (stored_index >= 0 && stored_index < displays)
            return stored_index;

        return 0;
    }
}
'''
write("src/main/sdl2/display_utils.hpp", display_utils)

# Persist preferred monitor identity directly in the existing XML tree. These
# DX extension methods are injected into Config as public methods.
config_methods = r'''    int preferred_display_index() \
    { \
        int index = cfg.get_int("video.display.index", 0); \
        return index < 0 ? 0 : index; \
    } \
    std::string preferred_display_name() \
    { \
        return cfg.get_string("video.display.name", ""); \
    } \
    void set_preferred_display(int index, const std::string& name) \
    { \
        if (index < 0) index = 0; \
        cfg.put_int("video.display.index", index); \
        cfg.put_string("video.display.name", name); \
    } \
'''
insert_before(
    "src/main/frontend/config.hpp",
    "    bool menu_sounds_enabled() \\\n",
    config_methods,
)

# RenderBase screen-size queries must target a requested display and must not
# cache display 0 forever.
replace_once(
    "src/main/sdl2/renderbase.hpp",
    "    bool sdl_screen_size();",
    "    bool sdl_screen_size(int display_index = 0);",
)
replace_once(
    "src/main/sdl2/renderbase.cpp",
    '''bool RenderBase::sdl_screen_size()
{
    if (orig_width == 0 || orig_height == 0)
    {
\tSDL_DisplayMode info;

\tSDL_GetCurrentDisplayMode(0, &info);

        orig_width  = info.w;
        orig_height = info.h;
    }

    scn_width  = orig_width;
    scn_height = orig_height;

    return true;
}
''',
    '''bool RenderBase::sdl_screen_size(int display_index)
{
    const int display_count = SDL_GetNumVideoDisplays();
    if (display_count <= 0)
        return false;

    if (display_index < 0 || display_index >= display_count)
        display_index = 0;

    SDL_DisplayMode info{};
    if (SDL_GetCurrentDisplayMode(display_index, &info) != 0)
    {
        std::cerr << "Unable to query display " << display_index
                  << ": " << SDL_GetError() << std::endl;
        return false;
    }

    orig_width  = static_cast<uint16_t>(info.w);
    orig_height = static_cast<uint16_t>(info.h);
    scn_width   = orig_width;
    scn_height  = orig_height;
    return true;
}
''',
)

# Renderer interface/state for idempotent display synchronization.
insert_after(
    "src/main/sdl2/rendersurface.hpp",
    "    void focus_window() override;\n",
    "    bool sync_display_state();\n    void mark_display_dirty() { display_dirty.store(true, std::memory_order_release); }\n",
)
insert_before(
    "src/main/sdl2/rendersurface.hpp",
    "    // constants\n",
    '''    int resolve_preferred_display() const;
    bool relocate_fullscreen_to_display(int display_index);
    static int SDLCALL display_event_watch(void* userdata, SDL_Event* event);

''',
)
insert_before(
    "src/main/sdl2/rendersurface.hpp",
    "    // LUTs for init_overlay()\n",
    '''    // Runtime monitor state. The preferred monitor remains in config.xml;
    // current_display only tracks where this live SDL window actually is.
    std::atomic<bool> display_dirty{true};
    bool display_event_watch_registered = false;
    int current_display = -1;
    int display_count_cache = 0;
    int drawable_width_cache = 0;
    int drawable_height_cache = 0;

''',
)

# Renderer implementation: target preferred display at startup, watch SDL
# display/window events, and synchronize only once from the render thread.
insert_after(
    "src/main/sdl2/rendersurface.cpp",
    '#include "frontend/config.hpp"\n',
    '#include "display_utils.hpp"\n',
)
insert_before(
    "src/main/sdl2/rendersurface.cpp",
    "void RenderSurface::create_buffers() {\n",
    r'''int RenderSurface::resolve_preferred_display() const
{
    return display_utils::resolve_preferred(
        config.preferred_display_index(),
        config.preferred_display_name());
}

int SDLCALL RenderSurface::display_event_watch(void* userdata, SDL_Event* event)
{
    auto* self = static_cast<RenderSurface*>(userdata);
    if (!self || !event)
        return 0;

    if (event->type == SDL_DISPLAYEVENT)
    {
        if (event->display.event == SDL_DISPLAYEVENT_CONNECTED ||
            event->display.event == SDL_DISPLAYEVENT_DISCONNECTED)
        {
            self->mark_display_dirty();
        }
        return 0;
    }

    if (event->type != SDL_WINDOWEVENT || !self->window)
        return 0;

    if (event->window.windowID != SDL_GetWindowID(self->window))
        return 0;

    switch (event->window.event)
    {
        case SDL_WINDOWEVENT_MOVED:
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
            self->mark_display_dirty();
            break;
        default:
            break;
    }

    return 0;
}

bool RenderSurface::relocate_fullscreen_to_display(int display_index)
{
    if (!window || video_mode == video_settings_t::MODE_WINDOW)
        return true;

    const int displays = SDL_GetNumVideoDisplays();
    if (display_index < 0 || display_index >= displays)
        return false;

    // Win+Shift+Arrow can move a borderless SDL window while retaining the old
    // monitor's dimensions. Leave fullscreen, place the window on the target
    // display, then re-enter the configured fullscreen mode there.
    if (SDL_SetWindowFullscreen(window, 0) != 0)
    {
        std::cerr << "Failed to leave fullscreen during display move: "
                  << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetWindowPosition(
        window,
        SDL_WINDOWPOS_CENTERED_DISPLAY(display_index),
        SDL_WINDOWPOS_CENTERED_DISPLAY(display_index));

    if (video_mode == video_settings_t::MODE_EXCLUSIVE)
    {
        SDL_DisplayMode desktop_mode{};
        if (SDL_GetDesktopDisplayMode(display_index, &desktop_mode) != 0)
        {
            std::cerr << "Failed to query target exclusive display mode: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        if (SDL_SetWindowDisplayMode(window, &desktop_mode) != 0)
        {
            std::cerr << "Failed to set target exclusive display mode: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) != 0)
        {
            std::cerr << "Failed to re-enter exclusive fullscreen: "
                      << SDL_GetError() << std::endl;
            return false;
        }
    }
    else if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
    {
        std::cerr << "Failed to re-enter borderless fullscreen: "
                  << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

bool RenderSurface::sync_display_state()
{
    if (!window || !initialised)
        return false;

    // Event callbacks only mark the state dirty. All SDL/GL mutation happens
    // here, once from the render thread after the event burst has settled.
    if (!display_dirty.exchange(false, std::memory_order_acq_rel))
        return false;

    const int displays = SDL_GetNumVideoDisplays();
    if (displays <= 0)
        return false;

    int display_index = SDL_GetWindowDisplayIndex(window);
    if (display_index < 0 || display_index >= displays)
        display_index = 0;

    const bool display_changed = display_index != current_display;

    if (display_changed && video_mode != video_settings_t::MODE_WINDOW)
    {
        const int target_display = display_index;
        if (!relocate_fullscreen_to_display(target_display))
        {
            // Retry on a later frame; the OS may still be settling a hotplug.
            mark_display_dirty();
            return false;
        }

        const int relocated_display = SDL_GetWindowDisplayIndex(window);
        display_index =
            (relocated_display >= 0 && relocated_display < displays)
                ? relocated_display
                : target_display;
    }

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0)
    {
        mark_display_dirty();
        return false;
    }

    const bool drawable_changed =
        drawable_width != drawable_width_cache ||
        drawable_height != drawable_height_cache;
    const bool display_count_changed = displays != display_count_cache;

    const int old_dst_width = dst_rect.w;
    const int old_dst_height = dst_rect.h;

    if (video_mode == video_settings_t::MODE_WINDOW)
    {
        // The windowed aspect-lock code decides the logical window dimensions.
        // Drawable pixels are authoritative here, especially across mixed-DPI
        // displays where the logical size may stay unchanged.
        scn_width = drawable_width;
        scn_height = drawable_height;
        dst_rect.x = 0;
        dst_rect.y = 0;
        dst_rect.w = drawable_width;
        dst_rect.h = drawable_height;
        anchor_x = 0;
        anchor_y = 0;
    }
    else
    {
        // Refresh the monitor mode rather than retaining the startup geometry.
        // Use the GL drawable as the final physical-pixel authority for DPI.
        if (!RenderBase::sdl_screen_size(display_index))
        {
            mark_display_dirty();
            return false;
        }

        orig_width = static_cast<uint16_t>(drawable_width);
        orig_height = static_cast<uint16_t>(drawable_height);
        set_scaling();
    }

    if (drawable_changed || display_changed || display_count_changed)
        glb::on_drawable_resized();

    if (old_dst_width != dst_rect.w || old_dst_height != dst_rect.h)
    {
        // Overlay storage and its geometry LUT depend on the destination size.
        // Rebuild only when that size really changed.
        const bool was_initialised = initialised;
        initialised = false;
        init_overlay();
        initialised = was_initialised;
    }

    current_display = display_index;
    display_count_cache = displays;
    drawable_width_cache = drawable_width;
    drawable_height_cache = drawable_height;

    return display_changed || drawable_changed || display_count_changed;
}

''',
)

# Remove event watch before the SDL window is destroyed.
insert_after(
    "src/main/sdl2/rendersurface.cpp",
    "void RenderSurface::disable()\n{\n",
    '''    if (display_event_watch_registered)
    {
        SDL_DelEventWatch(&RenderSurface::display_event_watch, this);
        display_event_watch_registered = false;
    }
    display_dirty.store(true, std::memory_order_release);
    current_display = -1;
    display_count_cache = 0;
    drawable_width_cache = 0;
    drawable_height_cache = 0;

''',
)

# Initial resolution and initial window position both use the resolved preference.
replace_once(
    "src/main/sdl2/rendersurface.cpp",
    '''    // First, determine our source and destination dimensions.
    // RenderBase::sdl_screen_size() should set orig_width and orig_height.
    if (!RenderBase::sdl_screen_size())
        return false;
''',
    '''    // Resolve the configured monitor before the window exists. Once the
    // window is live, runtime synchronization uses its actual display index.
    const int target_display = resolve_preferred_display();

    // First, determine our source and destination dimensions for that display.
    if (!RenderBase::sdl_screen_size(target_display))
        return false;
''',
)
replace_once(
    "src/main/sdl2/rendersurface.cpp",
    '''    window = SDL_CreateWindow("CannonBall DX",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        scn_width, scn_height, SDL_WINDOW_OPENGL);
''',
    '''    window = SDL_CreateWindow("CannonBall DX",
        SDL_WINDOWPOS_CENTERED_DISPLAY(target_display),
        SDL_WINDOWPOS_CENTERED_DISPLAY(target_display),
        scn_width, scn_height, SDL_WINDOW_OPENGL);
''',
)
insert_after(
    "src/main/sdl2/rendersurface.cpp",
    '''    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
''',
    '''
    current_display = SDL_GetWindowDisplayIndex(window);
    if (current_display < 0)
        current_display = target_display;
    display_count_cache = SDL_GetNumVideoDisplays();

    SDL_AddEventWatch(&RenderSurface::display_event_watch, this);
    display_event_watch_registered = true;
''',
)
insert_before(
    "src/main/sdl2/rendersurface.cpp",
    '''    // screen_pixels = static_cast<uint32_t*>(surface->pixels);
    return true;
''',
    '''    SDL_GL_GetDrawableSize(window, &drawable_width_cache, &drawable_height_cache);
    display_dirty.store(false, std::memory_order_release);

''',
)

# Keep the stock renderer safe too; PixelScalerRenderer has its own finalizer.
insert_after(
    "src/main/sdl2/rendersurface.cpp",
    '''    // ensure we have exclusive access to the SDL context
    std::lock_guard<std::mutex> gpulock(gpuMutex);

    int game_width = src_rect.w;
''',
    '''    sync_display_state();

''',
)

# PixelScalerRenderer is the active DX renderer. First enforce the windowed
# aspect lock, then perform one centralized display/drawable synchronization.
replace_once(
    "src/main/sdl2/pixelscaler_renderer.hpp",
    '''        sync_windowed_size();

        if (FrameCounter++ == 60)
''',
    '''        sync_windowed_size();
        sync_display_state();

        if (FrameCounter++ == 60)
''',
)
replace_once(
    "src/main/sdl2/pixelscaler_renderer.hpp",
    '''        int drawable_width = adjusted_width;
        int drawable_height = adjusted_height;
        SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);

        scn_width = drawable_width;
        scn_height = drawable_height;
        dst_rect.x = 0;
        dst_rect.y = 0;
        dst_rect.w = drawable_width;
        dst_rect.h = drawable_height;
        anchor_x = 0;
        anchor_y = 0;

        glb::on_drawable_resized();

        // init_overlay() normally rebuilds its LUT only when CRT geometry
        // changes. A window resize changes the LUT dimensions too, so force one
        // rebuild without permanently changing the renderer state.
        const bool was_initialised = initialised;
        initialised = false;
        init_overlay();
        initialised = was_initialised;

        scaler_last_config = -1;
        scaler_ticks = 3;
''',
    '''        // Defer drawable/viewport/overlay work to the central idempotent
        // monitor synchronizer. SDL_SetWindowSize can enqueue another event,
        // but the next sync becomes a no-op once the cached state matches.
        mark_display_dirty();
        scaler_last_config = -1;
        scaler_ticks = 3;
''',
)

# Per-monitor DPI awareness must be selected before SDL initializes VIDEO.
insert_after(
    "src/main/main.cpp",
    '''int main(int argc, char* argv[]) {
#ifdef __linux__
    install_segv_handler();
#endif
''',
    '''#ifdef _WIN32
    // Keep SDL drawable pixels aligned with the monitor the window is actually
    // on. Do not enable SDL_HINT_WINDOWS_DPI_SCALING: DX uses pixel coordinates.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
''',
)

# Video menu: show/cycle the preferred monitor without conflating it with the
# runtime current display. Hotplug is reflected by rebuilding the row text from
# SDL's live display list each menu tick.
insert_after(
    "src/main/frontend/menu.hpp",
    '#include "sdl2/gamepad_rumble_state.hpp"\n',
    '#include "sdl2/display_utils.hpp"\n',
)
insert_after(
    "src/main/frontend/menu.hpp",
    '    static constexpr const char* MENU_SOUNDS_LABEL = "MENU SOUNDS ";\n',
    '    static constexpr const char* DISPLAY_DEVICE_LABEL = "DISPLAY DEVICE ";\n',
)
menu_display_helpers = r'''    static std::string display_device_menu_text()
    {
        const int index = display_utils::resolve_preferred(
            config.preferred_display_index(),
            config.preferred_display_name());

        std::string display_name = audio_font_safe(display_utils::name(index));
        constexpr size_t max_name = 18;
        if (display_name.size() > max_name)
            display_name = display_name.substr(0, max_name - 3) + "...";

        std::string text = std::string(DISPLAY_DEVICE_LABEL) +
            std::to_string(index + 1);
        if (!display_name.empty())
            text += " " + display_name;
        return text;
    }

    void cycle_display_device(int direction)
    {
        const int displays = display_utils::count();
        if (displays <= 0)
            return;

        int index = display_utils::resolve_preferred(
            config.preferred_display_index(),
            config.preferred_display_name());
        index = (index + (direction < 0 ? -1 : 1) + displays) % displays;

        config.set_preferred_display(index, display_utils::name(index));
        config.videoRestartRequired = true;
    }

'''
insert_before(
    "src/main/frontend/menu.hpp",
    "    static const FfbMenuItem* dx_ffb_effect_items(int& count)\n",
    menu_display_helpers,
)

# Insert the monitor row after DISPLAY MODE when available, otherwise before
# ASPECT RATIO. This keeps compatibility with older/custom menu layouts.
insert_before(
    "src/main/frontend/menu.hpp",
    '''        // Gameplay is the natural home for detailed run rules and compatibility
''',
    r'''        const auto existing_display_device = std::find_if(
            menu_video.begin(), menu_video.end(),
            [](const std::string& entry)
            {
                return entry.rfind(DISPLAY_DEVICE_LABEL, 0) == 0;
            });
        if (existing_display_device == menu_video.end())
        {
            auto insert_pos = std::find_if(
                menu_video.begin(), menu_video.end(),
                [](const std::string& entry)
                {
                    return entry.rfind(ENTRY_FULLSCREEN, 0) == 0;
                });
            if (insert_pos != menu_video.end())
                ++insert_pos;
            else
                insert_pos = std::find_if(
                    menu_video.begin(), menu_video.end(),
                    [](const std::string& entry)
                    {
                        return entry.rfind(ENTRY_WIDESCREEN, 0) == 0;
                    });

            menu_video.insert(insert_pos, display_device_menu_text());
        }

''',
)

# Keep monitor label live for hotplug/name changes.
insert_before(
    "src/main/frontend/menu.cpp",
    '''    // Fundamental render scale stays on the VIDEO root.
''',
    r'''    if (!menu_video.empty())
    {
        for (std::string& entry : menu_video)
        {
            if (starts_with_label(entry, DISPLAY_DEVICE_LABEL))
                entry = display_device_menu_text();
        }
    }

''',
)

# LEFT/RIGHT on the monitor row.
insert_before(
    "src/main/frontend/menu.cpp",
    '''    // INPUT MODE uses left/right like the other value-style settings. Keyboard
''',
    r'''    if (menu_selected == &menu_video &&
        cursor >= 0 &&
        cursor < static_cast<int>(menu_video.size()) &&
        starts_with_label(menu_video[cursor], DISPLAY_DEVICE_LABEL) &&
        (input.has_pressed(Input::LEFT) || input.has_pressed(Input::RIGHT)))
    {
        cycle_display_device(input.has_pressed(Input::RIGHT) ? 1 : -1);
        menu_video[cursor] = display_device_menu_text();
        config_save_pending = true;
        osoundint.queue_sound(sound::BEEP1);
        return false;
    }

''',
)

# ENTER on the monitor row cycles forward, matching the other value rows.
insert_after(
    "src/main/frontend/menu.cpp",
    '''        const std::string& option = menu_video[cursor];

''',
    r'''        if (starts_with_label(option, DISPLAY_DEVICE_LABEL))
        {
            cycle_display_device(1);
            menu_video[cursor] = display_device_menu_text();
            return false;
        }

''',
)

print("Issue #36 multi-monitor patch applied successfully")
