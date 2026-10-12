/***************************************************************************
    On-screen touch controls - CannonBall DX (Android build only).

    An optional overlay of virtual buttons for touchscreens. The buttons drive the same logical Input key states as
    the keyboard, so the engine and frontend need no touch-specific code.

    Two independent layouts exist: the driving controls shown in game and
    the navigation controls shown in the frontend menus. Position, size and
    visibility of every button are user-editable and stored in config.xml.

    Rendering uses its own tiny GLES2 program and is drawn in screen space
    after the game image, directly before the buffer swap.
***************************************************************************/

#pragma once

#include <SDL.h>

#include <array>
#include <atomic>
#include <mutex>
#include <string>

class TouchControls
{
public:
    enum Page
    {
        PAGE_GAME = 0,
        PAGE_MENU = 1,
        PAGE_COUNT
    };

    // Load the settings and layout. Call after config.load() and input.init().
    void init();

    // Logic thread: SDL finger events and the once-per-frame state update.
    void handle_finger(const SDL_TouchFingerEvent* evt);
    void tick();

    // Render thread, with the GLES context current. needs_clear() tells the
    // renderer to clear the backbuffer before drawing the game image, because
    // translucent buttons in the letterbox area would otherwise accumulate.
    // render() is called just before the buffer swap; gl_release() before the
    // context is destroyed.
    bool needs_clear();
    void render(int fb_width, int fb_height);
    void gl_release();

    // Persistent settings. The caller is responsible for config.save().
    bool enabled();
    void set_enabled(bool value);
    bool hide_with_gamepad();
    void set_hide_with_gamepad(bool value);
    int  opacity();
    void set_opacity(int percent);

    // Gear shifting by touch: one button that toggles between low and high,
    // or two separate LO / HI buttons.
    int  gear_buttons();
    void set_gear_buttons(int count);
    void reset_layout();

    // Layout editor. While editing, touches move and resize the buttons
    // instead of pressing them. end_edit() writes the layout to the config
    // tree; the DONE button of the editor only clears editing().
    void begin_edit();
    void end_edit();
    bool editing();

    static constexpr int OPACITY_MIN = 10;
    static constexpr int OPACITY_MAX = 100;
    static constexpr int OPACITY_STEP = 10;

private:
    enum
    {
        // Driving layout
        EL_STEER_LEFT = 0,
        EL_STEER_RIGHT,
        EL_ACCEL,
        EL_BRAKE,
        EL_GEAR_LOW,
        EL_GEAR_HIGH,
        EL_GEAR_TOGGLE,
        EL_START,
        EL_COIN,
        EL_MENU,
        EL_VIEW,

        // Frontend layout
        EL_NAV_UP,
        EL_NAV_DOWN,
        EL_NAV_LEFT,
        EL_NAV_RIGHT,
        EL_NAV_ACCEPT,
        EL_NAV_BACK,

        EL_COUNT
    };

    struct Element
    {
        const char* key;    // config.xml name
        const char* label;  // text drawn on the button
        const char* name;   // description shown by the layout editor
        int page;
        int press[2];       // Input::presses driven by this button, -1 = unused

        // Centre in 0..1 of the screen; diameter as a fraction of its height.
        float x, y, size;
        bool visible;

        bool pressed;       // runtime state
    };

    enum FingerRole
    {
        ROLE_NONE,
        ROLE_DRAG,
        ROLE_PINCH
    };

    struct Finger
    {
        bool active = false;
        SDL_FingerID id = 0;
        float x = 0.0f;
        float y = 0.0f;
        int element = -1;

        // Set for touches that began before the current layout was shown.
        // They must be lifted before they can press anything.
        bool stale = false;

        FingerRole role = ROLE_NONE;
    };

    enum ToolbarButton
    {
        TB_PAGE = 0,
        TB_SMALLER,
        TB_LARGER,
        TB_VISIBLE,
        TB_RESET,
        TB_DONE,
        TB_COUNT
    };

    struct Rect
    {
        float x, y, w, h;   // pixels, top-left origin
    };

    static constexpr int MAX_FINGERS = 10;
    static constexpr float BUTTON_SIZE_MIN = 0.06f;
    static constexpr float BUTTON_SIZE_MAX = 0.60f;

    std::mutex mutex;

    std::array<Element, EL_COUNT> elements{};
    std::array<Finger, MAX_FINGERS> fingers{};

    bool initialised = false;
    bool setting_enabled = false;
    bool setting_hide_with_gamepad = true;
    int  setting_opacity = 50;
    int  setting_gear_buttons = 2;

    // Single gear button: the key it is currently holding down, and the
    // engine's gear as last seen by tick(), shown as the button label.
    int  gear_toggle_press = -1;
    bool gear_is_high = false;

    // Overlay state derived once per tick
    bool shown = false;
    int  page = PAGE_GAME;
    Uint32 wake_until = 0;

    // Layout editor
    bool edit_mode = false;
    int  edit_page = PAGE_GAME;
    int  edit_selected = -1;
    float drag_dx = 0.0f;
    float drag_dy = 0.0f;
    float pinch_start_distance = 0.0f;
    float pinch_start_size = 0.0f;

    // Last drawable size, published by the render thread for hit testing.
    std::atomic<int> screen_w{1920};
    std::atomic<int> screen_h{1080};

    // GL objects (render thread only)
    unsigned int gl_program = 0;
    unsigned int gl_vbo = 0;
    unsigned int gl_tex_button = 0;
    unsigned int gl_tex_font = 0;
    int gl_loc_tex = -1;
    bool gl_failed = false;

    void set_defaults(int page_filter);
    bool element_active(int index) const;
    void load_layout();
    void save_layout();

    Finger* find_finger(SDL_FingerID id);
    Finger* alloc_finger(SDL_FingerID id);
    int  hit_test(float x, float y, int on_page, bool include_hidden) const;
    void update_pressed();
    void release_all();
    void mark_fingers_stale();

    void editor_down(Finger& finger);
    void editor_motion(Finger& finger);
    void editor_up(Finger& finger);
    void editor_action(int button);
    Rect toolbar_rect(int button) const;
    int  toolbar_hit(float x, float y) const;
    float finger_distance(const Finger& a, const Finger& b) const;

    bool gl_init();
};

extern TouchControls touchcontrols;
