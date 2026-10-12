/***************************************************************************
    On-screen touch controls - CannonBall DX.

    See touchcontrols.hpp for an overview.
***************************************************************************/

#include "sdl2/touchcontrols.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include <SDL_opengles2.h>
#include <jni.h>

#include "main.hpp"
#include "frontend/config.hpp"
#include "sdl2/input.hpp"
#include "engine/oinputs.hpp"

TouchControls touchcontrols;

namespace
{
    // How long the overlay stays up after a touch while it is switched off or
    // hidden for a gamepad.
    const Uint32 WAKE_MS = 5000;

    // A touch slightly outside the drawn circle still counts as a press.
    const float HIT_SLOP = 1.12f;

    // Direction buttons carry one of these marker labels and are drawn as a
    // solid triangle instead of text.
    const char ARROW_LEFT[]  = "<";
    const char ARROW_RIGHT[] = ">";
    const char ARROW_UP[]    = "^";
    const char ARROW_DOWN[]  = "v";

    // ------------------------------------------------------------------------
    // 5x7 bitmap font. Glyphs live in 8x8 cells of a 128x32 texture.
    // ------------------------------------------------------------------------

    struct Glyph
    {
        char ch;
        const char* rows[7];
    };

    const Glyph FONT[] =
    {
        { ' ',    { ".....", ".....", ".....", ".....", ".....", ".....", "....." } },
        { 'A',    { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
        { 'B',    { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
        { 'C',    { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
        { 'D',    { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." } },
        { 'E',    { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
        { 'F',    { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
        { 'G',    { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###." } },
        { 'H',    { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
        { 'I',    { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." } },
        { 'J',    { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." } },
        { 'K',    { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
        { 'L',    { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
        { 'M',    { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" } },
        { 'N',    { "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#" } },
        { 'O',    { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
        { 'P',    { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
        { 'Q',    { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
        { 'R',    { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
        { 'S',    { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
        { 'T',    { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
        { 'U',    { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
        { 'V',    { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
        { 'W',    { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" } },
        { 'X',    { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
        { 'Y',    { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
        { 'Z',    { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
        { '0',    { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
        { '1',    { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
        { '2',    { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
        { '3',    { "####.", "....#", "....#", ".###.", "....#", "....#", "####." } },
        { '4',    { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." } },
        { '5',    { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
        { '6',    { ".###.", "#....", "#....", "####.", "#...#", "#...#", ".###." } },
        { '7',    { "#####", "....#", "...#.", "..#..", "..#..", "..#..", "..#.." } },
        { '8',    { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
        { '9',    { ".###.", "#...#", "#...#", ".####", "....#", "....#", ".###." } },
        { '-',    { ".....", ".....", ".....", "#####", ".....", ".....", "....." } },
        { '+',    { ".....", "..#..", "..#..", "#####", "..#..", "..#..", "....." } },
        { '/',    { "....#", "....#", "...#.", "..#..", ".#...", "#....", "#...." } },
        { '.',    { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." } },
        { '%',    { "##..#", "##.#.", "...#.", "..#..", ".#...", ".#.##", "#..##" } },
    };

    const int FONT_COUNT = static_cast<int>(sizeof(FONT) / sizeof(FONT[0]));
    const int FONT_TEX_W = 128;
    const int FONT_TEX_H = 32;
    const int FONT_CELL = 8;
    const int FONT_COLS = FONT_TEX_W / FONT_CELL;
    const int SOLID_CELL = (FONT_TEX_W / FONT_CELL) * (FONT_TEX_H / FONT_CELL) - 1;

    int glyph_cell(char ch)
    {
        for (int i = 0; i < FONT_COUNT; ++i)
        {
            if (FONT[i].ch == ch)
                return i;
        }
        return 0; // space
    }

    // Button sprite sheet: [0] idle, [1] pressed. White, alpha-only shapes.
    const int BUTTON_SPRITE = 128;
    const int BUTTON_TEX_W = BUTTON_SPRITE * 2;
    const int BUTTON_TEX_H = BUTTON_SPRITE;

    struct Color
    {
        float r, g, b, a;
    };

    // ------------------------------------------------------------------------
    // Vertex batching. Positions are given in pixels (top-left origin).
    // ------------------------------------------------------------------------

    struct Batch
    {
        std::vector<float> data;
        float inv_w = 1.0f;
        float inv_h = 1.0f;

        void vertex(float x, float y, float u, float v, const Color& c)
        {
            data.push_back(x * inv_w * 2.0f - 1.0f);
            data.push_back(1.0f - y * inv_h * 2.0f);
            data.push_back(u);
            data.push_back(v);
            data.push_back(c.r);
            data.push_back(c.g);
            data.push_back(c.b);
            data.push_back(c.a);
        }

        void quad(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1, const Color& c)
        {
            vertex(x0, y0, u0, v0, c);
            vertex(x1, y0, u1, v0, c);
            vertex(x1, y1, u1, v1, c);
            vertex(x0, y0, u0, v0, c);
            vertex(x1, y1, u1, v1, c);
            vertex(x0, y1, u0, v1, c);
        }

        int vertices() const
        {
            return static_cast<int>(data.size() / 8);
        }
    };

    void add_rect(Batch& batch, float x, float y, float w, float h, const Color& c)
    {
        const int col = SOLID_CELL % FONT_COLS;
        const int row = SOLID_CELL / FONT_COLS;
        const float u = (col * FONT_CELL + FONT_CELL * 0.5f) / FONT_TEX_W;
        const float v = (row * FONT_CELL + FONT_CELL * 0.5f) / FONT_TEX_H;
        batch.quad(x, y, x + w, y + h, u, v, u, v, c);
    }

    float text_width(const char* text, float scale)
    {
        const int length = static_cast<int>(std::char_traits<char>::length(text));
        return length > 0 ? (length * 6 - 1) * scale : 0.0f;
    }

    // Draw text centred on (cx, cy). scale is the size of one font pixel.
    void add_text(Batch& batch, float cx, float cy, float scale,
                  const char* text, const Color& c)
    {
        float x = cx - text_width(text, scale) * 0.5f;
        const float y = cy - 3.5f * scale;

        for (const char* p = text; *p; ++p)
        {
            const int cell = glyph_cell(*p);
            const int col = cell % FONT_COLS;
            const int row = cell / FONT_COLS;
            const float u0 = (col * FONT_CELL + 1.0f) / FONT_TEX_W;
            const float u1 = (col * FONT_CELL + 6.0f) / FONT_TEX_W;
            const float v0 = (row * FONT_CELL) / static_cast<float>(FONT_TEX_H);
            const float v1 = (row * FONT_CELL + 7.0f) / FONT_TEX_H;

            if (*p != ' ')
                batch.quad(x, y, x + 5.0f * scale, y + 7.0f * scale, u0, v0, u1, v1, c);

            x += 6.0f * scale;
        }
    }

    // Solid triangle pointing in the direction of the marker label.
    void add_arrow(Batch& batch, float cx, float cy, float size,
                   const char* marker, const Color& c)
    {
        const int col = SOLID_CELL % FONT_COLS;
        const int row = SOLID_CELL / FONT_COLS;
        const float u = (col * FONT_CELL + FONT_CELL * 0.5f) / FONT_TEX_W;
        const float v = (row * FONT_CELL + FONT_CELL * 0.5f) / FONT_TEX_H;

        // Unit triangle pointing right, rotated in quarter turns.
        float dx = 1.0f, dy = 0.0f;
        if (marker == ARROW_LEFT)       { dx = -1.0f; dy = 0.0f; }
        else if (marker == ARROW_UP)    { dx = 0.0f;  dy = -1.0f; }
        else if (marker == ARROW_DOWN)  { dx = 0.0f;  dy = 1.0f; }

        const float tip = size * 0.95f;
        const float back = size * 0.65f;
        batch.vertex(cx + dx * tip, cy + dy * tip, u, v, c);
        batch.vertex(cx - dx * back - dy * size, cy - dy * back + dx * size, u, v, c);
        batch.vertex(cx - dx * back + dy * size, cy - dy * back - dx * size, u, v, c);
    }

    bool is_arrow(const char* label)
    {
        return label == ARROW_LEFT || label == ARROW_RIGHT ||
               label == ARROW_UP || label == ARROW_DOWN;
    }

    // Largest font-pixel size at which the text fits the given box.
    float fit_text(const char* text, float max_width, float max_height)
    {
        const float unit_width = text_width(text, 1.0f);
        if (unit_width <= 0.0f)
            return 0.0f;
        return std::min(max_width / unit_width, max_height / 7.0f);
    }

    GLuint compile_shader(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[512] = {};
            glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
            std::cerr << "Touch controls shader failed: " << log << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    float clampf(float value, float low, float high)
    {
        return value < low ? low : (value > high ? high : value);
    }

    // Vibrate the phone through GameActivity.vibratePhone(), which marks the
    // vibration as game/media output. SDL's own haptic path is classed as
    // touch feedback by Android and is then scaled, or silenced, by the
    // user's system touch-feedback setting. duration_ms <= 0 stops it.
    void phone_vibrate(int duration_ms, float strength)
    {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity)
            return;

        static jmethodID method = nullptr;
        jclass activity_class = env->GetObjectClass(activity);
        if (!method)
        {
            method = env->GetMethodID(activity_class, "vibratePhone", "(IF)V");
            if (!method)
                env->ExceptionClear();
        }

        if (method)
        {
            env->CallVoidMethod(activity, method,
                                static_cast<jint>(duration_ms),
                                static_cast<jfloat>(strength));
            if (env->ExceptionCheck())
                env->ExceptionClear();
        }

        env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);
    }
}

// ----------------------------------------------------------------------------
// Settings and layout
// ----------------------------------------------------------------------------

void TouchControls::set_defaults(int page_filter)
{
    struct Default
    {
        const char* key;
        const char* label;
        const char* name;
        int page;
        int press0, press1;
        float x, y, size;
    };

    // Positions suit a landscape phone: steering under the left thumb, pedals
    // and gears under the right, system buttons along the top corners.
    static const Default DEFAULTS[EL_COUNT] =
    {
        { "steer_left",  ARROW_LEFT,  "STEER LEFT",  PAGE_GAME, Input::LEFT,      -1, 0.090f, 0.760f, 0.26f },
        { "steer_right", ARROW_RIGHT, "STEER RIGHT", PAGE_GAME, Input::RIGHT,     -1, 0.250f, 0.760f, 0.26f },
        { "accel",       "GAS",       "ACCELERATE",  PAGE_GAME, Input::ACCEL,     -1, 0.900f, 0.740f, 0.28f },
        { "brake",       "BRAKE",     "BRAKE",       PAGE_GAME, Input::BRAKE,     -1, 0.740f, 0.800f, 0.22f },
        { "gear_low",    "LO",        "GEAR LOW",    PAGE_GAME, Input::GEAR1,     -1, 0.930f, 0.440f, 0.16f },
        { "gear_high",   "HI",        "GEAR HIGH",   PAGE_GAME, Input::GEAR2,     -1, 0.930f, 0.250f, 0.16f },
        // Presses GEAR1 or GEAR2 depending on the current gear; see update_pressed().
        { "gear_toggle", "GEAR",      "GEAR SHIFT",  PAGE_GAME, -1,               -1, 0.930f, 0.380f, 0.20f },
        { "start",       "START",     "START",       PAGE_GAME, Input::START,     -1, 0.150f, 0.100f, 0.13f },
        { "coin",        "COIN",      "COIN",        PAGE_GAME, Input::COIN,      -1, 0.060f, 0.100f, 0.13f },
        { "menu",        "MENU",      "MENU",        PAGE_GAME, Input::MENU,      -1, 0.940f, 0.100f, 0.13f },
        { "view",        "VIEW",      "VIEW CHANGE", PAGE_GAME, Input::VIEWPOINT, -1, 0.060f, 0.360f, 0.14f },

        { "nav_up",      ARROW_UP,    "MENU UP",     PAGE_MENU, Input::UP,        -1, 0.140f, 0.520f, 0.19f },
        { "nav_down",    ARROW_DOWN,  "MENU DOWN",   PAGE_MENU, Input::DOWN,      -1, 0.140f, 0.860f, 0.19f },
        { "nav_left",    ARROW_LEFT,  "MENU LEFT",   PAGE_MENU, Input::LEFT,      -1, 0.065f, 0.690f, 0.19f },
        { "nav_right",   ARROW_RIGHT, "MENU RIGHT",  PAGE_MENU, Input::RIGHT,     -1, 0.215f, 0.690f, 0.19f },
        // The Time Trial selector inside the frontend confirms with START.
        { "nav_accept",  "OK",        "MENU ACCEPT", PAGE_MENU, Input::ACCEPT, Input::START, 0.900f, 0.760f, 0.26f },
        { "nav_back",    "BACK",      "MENU BACK",   PAGE_MENU, Input::BACK,      -1, 0.760f, 0.840f, 0.18f },
    };

    for (int i = 0; i < EL_COUNT; ++i)
    {
        const Default& d = DEFAULTS[i];
        if (page_filter >= 0 && d.page != page_filter)
            continue;

        Element& el = elements[i];
        el.key = d.key;
        el.label = d.label;
        el.name = d.name;
        el.page = d.page;
        el.press[0] = d.press0;
        el.press[1] = d.press1;
        el.x = d.x;
        el.y = d.y;
        el.size = d.size;
        el.visible = true;
        el.pressed = false;
    }
}

// Only one of the two gear button styles exists at a time.
bool TouchControls::element_active(int index) const
{
    if (index == EL_GEAR_TOGGLE)
        return setting_gear_buttons == 1;
    if (index == EL_GEAR_LOW || index == EL_GEAR_HIGH)
        return setting_gear_buttons != 1;
    return true;
}

void TouchControls::load_layout()
{
    for (Element& el : elements)
    {
        const std::string stored = config.touch_layout(el.key);
        int x = 0, y = 0, size = 0, visible = 1;

        if (std::sscanf(stored.c_str(), "%d,%d,%d,%d", &x, &y, &size, &visible) != 4)
            continue;

        el.x = clampf(x / 1000.0f, 0.0f, 1.0f);
        el.y = clampf(y / 1000.0f, 0.0f, 1.0f);
        el.size = clampf(size / 1000.0f, BUTTON_SIZE_MIN, BUTTON_SIZE_MAX);
        el.visible = visible != 0;
    }
}

void TouchControls::save_layout()
{
    for (const Element& el : elements)
    {
        char value[64] = {};
        std::snprintf(value, sizeof(value), "%d,%d,%d,%d",
                      static_cast<int>(std::lround(el.x * 1000.0f)),
                      static_cast<int>(std::lround(el.y * 1000.0f)),
                      static_cast<int>(std::lround(el.size * 1000.0f)),
                      el.visible ? 1 : 0);
        config.set_touch_layout(el.key, value);
    }
}

void TouchControls::init()
{
    std::lock_guard<std::mutex> lock(mutex);

    setting_enabled = config.touch_setting("enabled", 1) != 0;
    setting_hide_with_gamepad = config.touch_setting("hide_with_gamepad", 1) != 0;
    setting_opacity = std::clamp(
        config.touch_setting("opacity", 50), OPACITY_MIN, OPACITY_MAX);
    setting_gear_buttons = config.touch_setting("gear_buttons", 2) == 1 ? 1 : 2;
    setting_haptics = config.touch_setting("haptics", 1) != 0;
    setting_rumble = config.touch_setting("rumble", 1) != 0;
    setting_tilt = config.touch_setting("tilt", 0) != 0;
    setting_tilt_range = std::clamp(
        config.touch_setting("tilt_range", 30), TILT_RANGE_MIN, TILT_RANGE_MAX);

    set_defaults(-1);
    load_layout();
    initialised = true;
}

bool TouchControls::enabled()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setting_enabled;
}

void TouchControls::set_enabled(bool value)
{
    std::lock_guard<std::mutex> lock(mutex);
    setting_enabled = value;
    config.set_touch_setting("enabled", value ? 1 : 0);
}

bool TouchControls::hide_with_gamepad()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setting_hide_with_gamepad;
}

void TouchControls::set_hide_with_gamepad(bool value)
{
    std::lock_guard<std::mutex> lock(mutex);
    setting_hide_with_gamepad = value;
    config.set_touch_setting("hide_with_gamepad", value ? 1 : 0);
}

int TouchControls::opacity()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setting_opacity;
}

void TouchControls::set_opacity(int percent)
{
    std::lock_guard<std::mutex> lock(mutex);
    setting_opacity = std::clamp(percent, OPACITY_MIN, OPACITY_MAX);
    config.set_touch_setting("opacity", setting_opacity);
}

int TouchControls::gear_buttons()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setting_gear_buttons;
}

void TouchControls::set_gear_buttons(int count)
{
    std::lock_guard<std::mutex> lock(mutex);
    release_all();
    mark_fingers_stale();
    setting_gear_buttons = count == 1 ? 1 : 2;
    config.set_touch_setting("gear_buttons", setting_gear_buttons);
}

bool TouchControls::haptics_enabled()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setting_haptics;
}

void TouchControls::set_haptics_enabled(bool value)
{
    std::lock_guard<std::mutex> lock(mutex);
    setting_haptics = value;
    config.set_touch_setting("haptics", value ? 1 : 0);
    if (value)
        pulse();
}

bool TouchControls::rumble_enabled()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setting_rumble;
}

void TouchControls::set_rumble_enabled(bool value)
{
    std::lock_guard<std::mutex> lock(mutex);
    setting_rumble = value;
    config.set_touch_setting("rumble", value ? 1 : 0);
}

// Short tick confirming a touch-button press.
void TouchControls::pulse()
{
    if (!setting_haptics)
        return;

    phone_vibrate(18, 0.6f);

    // The tick replaced any running rumble; let the mixer restart it.
    rumble_issued_at = 0;
    rumble_strength = 0.0f;
}

int TouchControls::rumble(Uint16 low_frequency, Uint16 high_frequency, Uint32 duration_ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    const float strength = !initialised || !setting_rumble || duration_ms == 0
        ? 0.0f
        : std::max(low_frequency, high_frequency) / 65535.0f;

    if (strength <= 0.0f)
    {
        if (rumble_playing)
            phone_vibrate(0, 0.0f);
        rumble_playing = false;
        rumble_strength = 0.0f;
        return 0;
    }

    // The mixer calls this on every logic tick. Restarting the vibrator that
    // often makes it stutter, so a request runs for a while and is renewed
    // only shortly before it ends or when the strength really changes.
    const Uint32 now = SDL_GetTicks();
    const bool renew =
        !rumble_playing ||
        now - rumble_issued_at >= 160 ||
        std::fabs(strength - rumble_strength) > 0.10f;

    if (renew)
    {
        phone_vibrate(240, strength);
        rumble_playing = true;
        rumble_strength = strength;
        rumble_issued_at = now;
    }

    return 0;
}

bool TouchControls::tilt_enabled()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setting_tilt;
}

void TouchControls::set_tilt_enabled(bool value)
{
    std::lock_guard<std::mutex> lock(mutex);
    setting_tilt = value;
    config.set_touch_setting("tilt", value ? 1 : 0);
}

int TouchControls::tilt_range()
{
    std::lock_guard<std::mutex> lock(mutex);
    return setting_tilt_range;
}

void TouchControls::set_tilt_range(int degrees)
{
    std::lock_guard<std::mutex> lock(mutex);
    setting_tilt_range = std::clamp(degrees, TILT_RANGE_MIN, TILT_RANGE_MAX);
    config.set_touch_setting("tilt_range", setting_tilt_range);
}

// Read the accelerometer and derive how far the phone is rotated about the
// axis through the screen, like a steering wheel. The sensor is only kept
// open while tilt steering can actually be used.
void TouchControls::update_tilt()
{
    const bool wanted = setting_tilt && !input.has_game_controller();

    if (!wanted)
    {
        if (accelerometer)
        {
            SDL_SensorClose(accelerometer);
            accelerometer = nullptr;
        }
        tilt_valid = false;
        return;
    }

    if (!accelerometer)
    {
        if ((SDL_WasInit(SDL_INIT_SENSOR) & SDL_INIT_SENSOR) == 0 &&
            SDL_InitSubSystem(SDL_INIT_SENSOR) != 0)
        {
            return;
        }

        const int count = SDL_NumSensors();
        for (int i = 0; i < count && !accelerometer; ++i)
        {
            if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_ACCEL)
                accelerometer = SDL_SensorOpen(i);
        }

        if (!accelerometer)
            return;
    }

    float data[3] = {};
    if (SDL_SensorGetData(accelerometer, data, 3) != 0)
        return;

    // data[0] / data[1] are the device's own right / up axes. With the phone
    // lying nearly flat the direction of "up" in the screen plane is noise,
    // so keep the last value until it is held up again.
    const float ax = data[0];
    const float ay = data[1];
    if (ax * ax + ay * ay < 9.0f)
        return;

    // Angle of "up" in device coordinates: +90 or -90 degrees in the two
    // landscape orientations. Turning the phone clockwise lowers it, so the
    // steering angle is the distance from whichever landscape is nearer.
    const float up = std::atan2(ax, ay) * 57.29578f;
    const float steer = (up >= 0.0f ? 90.0f : -90.0f) - up;

    if (!tilt_valid)
        tilt_degrees = steer;
    else
        tilt_degrees += (steer - tilt_degrees) * 0.35f;

    tilt_valid = true;
}

void TouchControls::apply_tilt_steering()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!initialised || !setting_tilt || !tilt_valid ||
        cannonball::state != cannonball::STATE_GAME)
    {
        return;
    }

    // Steering buttons, where still shown, take priority while held.
    if (input.is_pressed(Input::LEFT) || input.is_pressed(Input::RIGHT))
        return;

    const float dead_zone = 1.5f;
    float angle = tilt_degrees;
    if (std::fabs(angle) <= dead_zone)
        angle = 0.0f;
    else
        angle -= angle > 0.0f ? dead_zone : -dead_zone;

    const float amount =
        clampf(angle / (setting_tilt_range - dead_zone), -1.0f, 1.0f);

    // Same centre and lock as OInputs::STEERING_CENTRE / STEERING_MAX, which
    // are private to the engine class.
    const int steering_centre = 0x80;
    const float lock_range = 0x38;

    oinputs.input_steering =
        steering_centre + static_cast<int>(std::lround(amount * lock_range));
}

void TouchControls::reset_layout()
{
    std::lock_guard<std::mutex> lock(mutex);
    release_all();
    mark_fingers_stale();
    set_defaults(-1);
    save_layout();
}

// ----------------------------------------------------------------------------
// Touch input
// ----------------------------------------------------------------------------

TouchControls::Finger* TouchControls::find_finger(SDL_FingerID id)
{
    for (Finger& finger : fingers)
    {
        if (finger.active && finger.id == id)
            return &finger;
    }
    return nullptr;
}

TouchControls::Finger* TouchControls::alloc_finger(SDL_FingerID id)
{
    if (Finger* existing = find_finger(id))
        return existing;

    for (Finger& finger : fingers)
    {
        if (!finger.active)
        {
            finger = Finger{};
            finger.active = true;
            finger.id = id;
            return &finger;
        }
    }
    return nullptr;
}

int TouchControls::hit_test(float x, float y, int on_page, bool include_hidden) const
{
    const float w = static_cast<float>(screen_w.load(std::memory_order_relaxed));
    const float h = static_cast<float>(screen_h.load(std::memory_order_relaxed));

    int best = -1;
    float best_distance = 0.0f;

    for (int i = 0; i < EL_COUNT; ++i)
    {
        const Element& el = elements[i];
        if (el.page != on_page || !element_active(i) ||
            (!el.visible && !include_hidden))
        {
            continue;
        }

        const float dx = (x - el.x) * w;
        const float dy = (y - el.y) * h;
        const float radius = el.size * h * 0.5f * HIT_SLOP;
        const float distance = dx * dx + dy * dy;

        if (distance <= radius * radius && (best < 0 || distance < best_distance))
        {
            best = i;
            best_distance = distance;
        }
    }

    return best;
}

void TouchControls::update_pressed()
{
    bool now_pressed[EL_COUNT] = {};

    for (const Finger& finger : fingers)
    {
        if (finger.active && !finger.stale && finger.element >= 0)
            now_pressed[finger.element] = true;
    }

    for (int i = 0; i < EL_COUNT; ++i)
    {
        Element& el = elements[i];
        if (el.pressed == now_pressed[i])
            continue;

        el.pressed = now_pressed[i];
        if (el.pressed)
            pulse();

        if (i == EL_GEAR_TOGGLE)
        {
            // With separate gear buttons configured in the game, the single
            // touch button presses whichever one changes the current gear.
            // In the other gear modes GEAR1 already toggles (or is held).
            if (el.pressed)
            {
                gear_toggle_press =
                    (config.controls.gear == config.controls.GEAR_SEPARATE &&
                     !oinputs.gear)
                        ? Input::GEAR2
                        : Input::GEAR1;
                input.set_touch_key(
                    static_cast<Input::presses>(gear_toggle_press), true);
            }
            else if (gear_toggle_press >= 0)
            {
                input.set_touch_key(
                    static_cast<Input::presses>(gear_toggle_press), false);
                gear_toggle_press = -1;
            }
            continue;
        }

        for (int press : el.press)
        {
            if (press >= 0)
                input.set_touch_key(static_cast<Input::presses>(press), el.pressed);
        }
    }
}

void TouchControls::release_all()
{
    for (Finger& finger : fingers)
        finger.element = -1;

    update_pressed();
}

void TouchControls::mark_fingers_stale()
{
    for (Finger& finger : fingers)
    {
        if (finger.active)
        {
            finger.stale = true;
            finger.role = ROLE_NONE;
        }
    }
}

void TouchControls::handle_finger(const SDL_TouchFingerEvent* evt)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!initialised || !evt)
        return;

    if (evt->type == SDL_FINGERDOWN)
    {
        Finger* finger = alloc_finger(evt->fingerId);
        if (!finger)
            return;

        finger->x = evt->x;
        finger->y = evt->y;
        finger->element = -1;
        finger->role = ROLE_NONE;
        finger->stale = false;

        Uint32 wake = SDL_GetTicks() + WAKE_MS;
        if (wake == 0)
            wake = 1;

        if (!shown)
        {
            // The controls are switched off or hidden for a gamepad. A touch
            // brings them back for a moment, so a touch-only device can never
            // be locked out of the menus. This first touch presses nothing.
            wake_until = wake;
            finger->stale = true;
            return;
        }

        if (wake_until != 0)
            wake_until = wake;

        if (edit_mode)
        {
            editor_down(*finger);
        }
        else
        {
            finger->element = hit_test(finger->x, finger->y, page, false);
            update_pressed();
        }
    }
    else if (evt->type == SDL_FINGERMOTION)
    {
        Finger* finger = find_finger(evt->fingerId);
        if (!finger)
            return;

        finger->x = evt->x;
        finger->y = evt->y;

        if (edit_mode)
        {
            editor_motion(*finger);
        }
        else if (shown && !finger->stale)
        {
            // Re-evaluated on every move so a thumb can slide between the
            // steering buttons without being lifted.
            finger->element = hit_test(finger->x, finger->y, page, false);
            update_pressed();
        }
    }
    else if (evt->type == SDL_FINGERUP)
    {
        Finger* finger = find_finger(evt->fingerId);
        if (!finger)
            return;

        if (edit_mode)
            editor_up(*finger);

        finger->active = false;
        finger->element = -1;
        finger->role = ROLE_NONE;

        if (!edit_mode)
            update_pressed();
    }
}

void TouchControls::tick()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!initialised)
        return;

    if (wake_until != 0 && SDL_TICKS_PASSED(SDL_GetTicks(), wake_until))
        wake_until = 0;

    const bool configured =
        setting_enabled &&
        !(setting_hide_with_gamepad && input.has_game_controller());

    const bool want_shown = edit_mode || configured || wake_until != 0;

    gear_is_high = oinputs.gear;
    update_tilt();

    const bool frontend =
        cannonball::state == cannonball::STATE_MENU ||
        cannonball::state == cannonball::STATE_INIT_MENU;

    const int want_page =
        edit_mode ? edit_page : (frontend ? PAGE_MENU : PAGE_GAME);

    if (want_shown != shown || want_page != page)
    {
        // Never carry a held button from one layout into the other: release
        // everything and require the fingers to be lifted first.
        release_all();
        mark_fingers_stale();
        shown = want_shown;
        page = want_page;
    }
}

// ----------------------------------------------------------------------------
// Layout editor
// ----------------------------------------------------------------------------

void TouchControls::begin_edit()
{
    std::lock_guard<std::mutex> lock(mutex);
    release_all();
    mark_fingers_stale();
    edit_mode = true;
    edit_page = PAGE_GAME;
    edit_selected = -1;
}

void TouchControls::end_edit()
{
    std::lock_guard<std::mutex> lock(mutex);
    edit_mode = false;
    edit_selected = -1;
    mark_fingers_stale();
    save_layout();
}

bool TouchControls::editing()
{
    std::lock_guard<std::mutex> lock(mutex);
    return edit_mode;
}

TouchControls::Rect TouchControls::toolbar_rect(int button) const
{
    const float w = static_cast<float>(screen_w.load(std::memory_order_relaxed));
    const float h = static_cast<float>(screen_h.load(std::memory_order_relaxed));

    // Two rows of three buttons in the middle of the screen, which the
    // default layouts leave free.
    const float bw = 0.26f * h;
    const float bh = 0.11f * h;
    const float gap = 0.02f * h;
    const float total_w = 3.0f * bw + 2.0f * gap;
    const float total_h = 2.0f * bh + gap;

    Rect rect;
    rect.x = (w - total_w) * 0.5f + (button % 3) * (bw + gap);
    rect.y = (h - total_h) * 0.5f + (button / 3) * (bh + gap);
    rect.w = bw;
    rect.h = bh;
    return rect;
}

int TouchControls::toolbar_hit(float x, float y) const
{
    const float px = x * screen_w.load(std::memory_order_relaxed);
    const float py = y * screen_h.load(std::memory_order_relaxed);

    for (int i = 0; i < TB_COUNT; ++i)
    {
        const Rect rect = toolbar_rect(i);
        if (px >= rect.x && px <= rect.x + rect.w &&
            py >= rect.y && py <= rect.y + rect.h)
        {
            return i;
        }
    }
    return -1;
}

float TouchControls::finger_distance(const Finger& a, const Finger& b) const
{
    const float dx = (a.x - b.x) * screen_w.load(std::memory_order_relaxed);
    const float dy = (a.y - b.y) * screen_h.load(std::memory_order_relaxed);
    return std::sqrt(dx * dx + dy * dy);
}

void TouchControls::editor_action(int button)
{
    Element* selected = edit_selected >= 0 ? &elements[edit_selected] : nullptr;

    switch (button)
    {
        case TB_PAGE:
            edit_page = edit_page == PAGE_GAME ? PAGE_MENU : PAGE_GAME;
            edit_selected = -1;
            for (Finger& finger : fingers)
                finger.role = ROLE_NONE;
            break;

        case TB_SMALLER:
            if (selected)
                selected->size = clampf(selected->size / 1.1f, BUTTON_SIZE_MIN, BUTTON_SIZE_MAX);
            break;

        case TB_LARGER:
            if (selected)
                selected->size = clampf(selected->size * 1.1f, BUTTON_SIZE_MIN, BUTTON_SIZE_MAX);
            break;

        case TB_VISIBLE:
            if (selected)
                selected->visible = !selected->visible;
            break;

        case TB_RESET:
            set_defaults(edit_page);
            edit_selected = -1;
            for (Finger& finger : fingers)
                finger.role = ROLE_NONE;
            break;

        case TB_DONE:
            // The menu notices this on its next tick and saves the layout.
            edit_mode = false;
            break;
    }
}

void TouchControls::editor_down(Finger& finger)
{
    const int button = toolbar_hit(finger.x, finger.y);
    if (button >= 0)
    {
        editor_action(button);
        return;
    }

    // A second finger while a button is being dragged turns into a pinch.
    Finger* dragging = nullptr;
    for (Finger& other : fingers)
    {
        if (other.active && &other != &finger && other.role == ROLE_DRAG)
            dragging = &other;
    }

    if (dragging && edit_selected >= 0)
    {
        finger.role = ROLE_PINCH;
        pinch_start_distance = std::max(1.0f, finger_distance(*dragging, finger));
        pinch_start_size = elements[edit_selected].size;
        return;
    }

    const int hit = hit_test(finger.x, finger.y, edit_page, true);
    if (hit < 0)
        return;

    edit_selected = hit;
    finger.role = ROLE_DRAG;
    drag_dx = elements[hit].x - finger.x;
    drag_dy = elements[hit].y - finger.y;
}

void TouchControls::editor_motion(Finger& finger)
{
    if (edit_selected < 0 || finger.role == ROLE_NONE)
        return;

    Element& el = elements[edit_selected];

    Finger* dragging = nullptr;
    Finger* pinching = nullptr;
    for (Finger& other : fingers)
    {
        if (!other.active)
            continue;
        if (other.role == ROLE_DRAG)
            dragging = &other;
        else if (other.role == ROLE_PINCH)
            pinching = &other;
    }

    if (dragging && pinching)
    {
        const float distance = finger_distance(*dragging, *pinching);
        el.size = clampf(
            pinch_start_size * distance / pinch_start_distance,
            BUTTON_SIZE_MIN, BUTTON_SIZE_MAX);
        return;
    }

    if (finger.role == ROLE_DRAG)
    {
        el.x = clampf(finger.x + drag_dx, 0.0f, 1.0f);
        el.y = clampf(finger.y + drag_dy, 0.0f, 1.0f);
    }
}

void TouchControls::editor_up(Finger& finger)
{
    if (finger.role == ROLE_PINCH)
    {
        // Continue the drag from where the remaining finger is now, so the
        // button does not jump when the pinch ends.
        for (Finger& other : fingers)
        {
            if (other.active && other.role == ROLE_DRAG && edit_selected >= 0)
            {
                drag_dx = elements[edit_selected].x - other.x;
                drag_dy = elements[edit_selected].y - other.y;
            }
        }
    }
    else if (finger.role == ROLE_DRAG)
    {
        for (Finger& other : fingers)
        {
            if (other.role == ROLE_PINCH)
                other.role = ROLE_NONE;
        }
    }

    finger.role = ROLE_NONE;
}

// ----------------------------------------------------------------------------
// Rendering
// ----------------------------------------------------------------------------

bool TouchControls::needs_clear()
{
    // Always: translucent buttons in the letterbox area would otherwise
    // accumulate from frame to frame, and the surface is recreated whenever
    // the app returns from the background, leaving undefined pixels there.
    return true;
}

bool TouchControls::gl_init()
{
    if (gl_program)
        return true;
    if (gl_failed)
        return false;

    static const char* VERTEX_SOURCE =
        "attribute vec2 aPosition;\n"
        "attribute vec2 aTexCoord;\n"
        "attribute vec4 aColor;\n"
        "varying vec2 vTexCoord;\n"
        "varying vec4 vColor;\n"
        "void main(){\n"
        "    vTexCoord = aTexCoord;\n"
        "    vColor = aColor;\n"
        "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
        "}\n";

    static const char* FRAGMENT_SOURCE =
        "precision mediump float;\n"
        "varying vec2 vTexCoord;\n"
        "varying vec4 vColor;\n"
        "uniform sampler2D uTexture;\n"
        "void main(){\n"
        "    gl_FragColor = texture2D(uTexture, vTexCoord) * vColor;\n"
        "}\n";

    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, VERTEX_SOURCE);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SOURCE);

    if (vertex && fragment)
    {
        gl_program = glCreateProgram();
        glAttachShader(gl_program, vertex);
        glAttachShader(gl_program, fragment);
        glBindAttribLocation(gl_program, 0, "aPosition");
        glBindAttribLocation(gl_program, 1, "aTexCoord");
        glBindAttribLocation(gl_program, 2, "aColor");
        glLinkProgram(gl_program);

        GLint ok = 0;
        glGetProgramiv(gl_program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            std::cerr << "Touch controls shader link failed." << std::endl;
            glDeleteProgram(gl_program);
            gl_program = 0;
        }
    }

    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);

    if (!gl_program)
    {
        gl_failed = true;
        return false;
    }

    gl_loc_tex = glGetUniformLocation(gl_program, "uTexture");
    glGenBuffers(1, &gl_vbo);

    // Button sprites: a ring with a translucent fill, idle and pressed.
    std::vector<uint8_t> pixels(static_cast<size_t>(BUTTON_TEX_W) * BUTTON_TEX_H * 4);
    const float centre = (BUTTON_SPRITE - 1) * 0.5f;
    const float outer = BUTTON_SPRITE * 0.5f - 2.0f;
    const float inner = outer * 0.88f;

    for (int sprite = 0; sprite < 2; ++sprite)
    {
        const float fill = sprite == 0 ? 0.26f : 0.62f;

        for (int y = 0; y < BUTTON_SPRITE; ++y)
        {
            for (int x = 0; x < BUTTON_SPRITE; ++x)
            {
                const float dx = x - centre;
                const float dy = y - centre;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float edge = clampf(outer - d + 0.5f, 0.0f, 1.0f);
                const float ring = clampf(d - inner + 0.5f, 0.0f, 1.0f);
                const float alpha = edge * (fill + (1.0f - fill) * ring);

                uint8_t* p = &pixels[(static_cast<size_t>(y) * BUTTON_TEX_W +
                                      sprite * BUTTON_SPRITE + x) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
            }
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glGenTextures(1, &gl_tex_button);
    glBindTexture(GL_TEXTURE_2D, gl_tex_button);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BUTTON_TEX_W, BUTTON_TEX_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Font atlas. Nearest filtering keeps the labels as crisp pixel art.
    pixels.assign(static_cast<size_t>(FONT_TEX_W) * FONT_TEX_H * 4, 0);
    auto set_pixel = [&](int x, int y)
    {
        uint8_t* p = &pixels[(static_cast<size_t>(y) * FONT_TEX_W + x) * 4];
        p[0] = p[1] = p[2] = p[3] = 255;
    };

    for (int i = 0; i < FONT_COUNT; ++i)
    {
        const int cell_x = (i % FONT_COLS) * FONT_CELL;
        const int cell_y = (i / FONT_COLS) * FONT_CELL;

        for (int row = 0; row < 7; ++row)
        {
            for (int col = 0; col < 5; ++col)
            {
                if (FONT[i].rows[row][col] == '#')
                    set_pixel(cell_x + 1 + col, cell_y + row);
            }
        }
    }

    for (int y = 0; y < FONT_CELL; ++y)
    {
        for (int x = 0; x < FONT_CELL; ++x)
        {
            set_pixel((SOLID_CELL % FONT_COLS) * FONT_CELL + x,
                      (SOLID_CELL / FONT_COLS) * FONT_CELL + y);
        }
    }

    glGenTextures(1, &gl_tex_font);
    glBindTexture(GL_TEXTURE_2D, gl_tex_font);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FONT_TEX_W, FONT_TEX_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    return true;
}

void TouchControls::gl_release()
{
    if (gl_tex_button) glDeleteTextures(1, &gl_tex_button);
    if (gl_tex_font)   glDeleteTextures(1, &gl_tex_font);
    if (gl_vbo)        glDeleteBuffers(1, &gl_vbo);
    if (gl_program)    glDeleteProgram(gl_program);

    gl_tex_button = 0;
    gl_tex_font = 0;
    gl_vbo = 0;
    gl_program = 0;
    gl_loc_tex = -1;
    gl_failed = false;
}

void TouchControls::render(int fb_width, int fb_height)
{
    if (fb_width <= 0 || fb_height <= 0)
        return;

    screen_w.store(fb_width, std::memory_order_relaxed);
    screen_h.store(fb_height, std::memory_order_relaxed);

    // Take a snapshot so the logic thread is never blocked by GL calls.
    std::array<Element, EL_COUNT> snapshot;
    bool editing_now = false;
    int  shown_page = PAGE_GAME;
    int  selected = -1;
    int  opacity_percent = 50;
    bool gear_high = false;
    bool active[EL_COUNT] = {};

    {
        std::lock_guard<std::mutex> lock(mutex);

        if (!initialised || !shown)
            return;

        snapshot = elements;
        editing_now = edit_mode;
        shown_page = edit_mode ? edit_page : page;
        selected = edit_selected;
        opacity_percent = setting_opacity;
        gear_high = gear_is_high;
        for (int i = 0; i < EL_COUNT; ++i)
            active[i] = element_active(i);
    }

    if (!gl_init())
        return;

    const float w = static_cast<float>(fb_width);
    const float h = static_cast<float>(fb_height);

    // The editor must stay readable even with a very low opacity setting.
    float opacity = opacity_percent / 100.0f;
    if (editing_now)
        opacity = std::max(opacity, 0.6f);

    Batch buttons;
    Batch glyphs;
    buttons.inv_w = glyphs.inv_w = 1.0f / w;
    buttons.inv_h = glyphs.inv_h = 1.0f / h;

    for (int i = 0; i < EL_COUNT; ++i)
    {
        const Element& el = snapshot[i];
        if (el.page != shown_page || !active[i] || (!el.visible && !editing_now))
            continue;

        // The single gear button shows the gear the car is in.
        const char* label = el.label;
        if (i == EL_GEAR_TOGGLE && !editing_now)
            label = gear_high ? "HI" : "LO";

        Color color = { 1.0f, 1.0f, 1.0f, opacity };
        if (editing_now && i == selected)
            color = { 1.0f, 0.85f, 0.2f, opacity };
        if (!el.visible)
            color.a *= 0.35f;

        const float diameter = el.size * h;
        const float cx = el.x * w;
        const float cy = el.y * h;
        const float u0 = el.pressed ? 0.5f : 0.0f;

        buttons.quad(cx - diameter * 0.5f, cy - diameter * 0.5f,
                     cx + diameter * 0.5f, cy + diameter * 0.5f,
                     u0, 0.0f, u0 + 0.5f, 1.0f, color);

        if (is_arrow(label))
        {
            add_arrow(glyphs, cx, cy, diameter * 0.20f, label, color);
        }
        else
        {
            add_text(glyphs, cx, cy,
                     fit_text(label, diameter * 0.66f, diameter * 0.30f),
                     label, color);
        }
    }

    if (editing_now)
    {
        const Rect first = toolbar_rect(0);
        const Rect last = toolbar_rect(TB_COUNT - 1);
        const float line = 0.034f * h;
        const float pad = 0.02f * h;

        const float panel_x = first.x - pad;
        const float panel_y = first.y - pad - 3.0f * line;
        const float panel_w = (last.x + last.w + pad) - panel_x;
        const float panel_h = (last.y + last.h + pad + 1.6f * line) - panel_y;
        const float centre_x = panel_x + panel_w * 0.5f;

        add_rect(glyphs, panel_x, panel_y, panel_w, panel_h, { 0.0f, 0.0f, 0.0f, 0.60f });

        const Color text_color = { 1.0f, 1.0f, 1.0f, 0.95f };
        const char* line1 = "DRAG A BUTTON TO MOVE IT";
        const char* line2 = "PINCH OR USE SIZE TO RESIZE IT";
        const float text_scale = std::min(
            fit_text(line2, panel_w - 2.0f * pad, line), line / 7.0f * 0.8f);

        add_text(glyphs, centre_x, panel_y + pad + line * 0.5f, text_scale, line1, text_color);
        add_text(glyphs, centre_x, panel_y + pad + line * 1.8f, text_scale, line2, text_color);

        const char* status =
            selected >= 0 ? snapshot[selected].name : "TAP A BUTTON TO SELECT IT";
        add_text(glyphs, centre_x, last.y + last.h + pad * 0.5f + line * 0.7f,
                 text_scale, status,
                 selected >= 0 ? Color{ 1.0f, 0.85f, 0.2f, 0.95f } : text_color);

        const bool selected_visible = selected < 0 || snapshot[selected].visible;
        const char* labels[TB_COUNT] =
        {
            shown_page == PAGE_GAME ? "GAME PAGE" : "MENU PAGE",
            "SIZE -",
            "SIZE +",
            selected_visible ? "HIDE" : "SHOW",
            "RESET",
            "DONE",
        };

        for (int i = 0; i < TB_COUNT; ++i)
        {
            const Rect rect = toolbar_rect(i);
            const bool needs_selection =
                i == TB_SMALLER || i == TB_LARGER || i == TB_VISIBLE;
            const float alpha = needs_selection && selected < 0 ? 0.35f : 0.90f;

            Color fill = { 0.25f, 0.25f, 0.30f, alpha };
            if (i == TB_DONE)
                fill = { 0.10f, 0.45f, 0.20f, alpha };

            add_rect(glyphs, rect.x, rect.y, rect.w, rect.h, fill);
            add_text(glyphs, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f,
                     fit_text(labels[i], rect.w * 0.82f, rect.h * 0.42f),
                     labels[i], { 1.0f, 1.0f, 1.0f, alpha });
        }
    }

    if (buttons.vertices() == 0 && glyphs.vertices() == 0)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb_width, fb_height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(gl_program);
    glUniform1i(gl_loc_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo);

    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(4 * sizeof(float)));

    auto draw_batch = [](const Batch& batch, GLuint texture)
    {
        if (batch.vertices() == 0)
            return;

        glBindTexture(GL_TEXTURE_2D, texture);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(batch.data.size() * sizeof(float)),
                     batch.data.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, batch.vertices());
    };

    draw_batch(buttons, gl_tex_button);
    draw_batch(glyphs, gl_tex_font);

    // Leave the state the game renderer expects. It rebinds its own program,
    // textures, buffer and attribute pointers for every frame.
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
}
