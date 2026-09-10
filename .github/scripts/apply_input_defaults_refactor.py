from pathlib import Path


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, text):
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}\n--- OLD ---\n{old}")
    write(path, text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# Config helpers: clearing a default-backed physical binding must suppress the
# default instead of making it silently return on the next frame/startup.
# ---------------------------------------------------------------------------
replace_once(
    "src/main/frontend/config.hpp",
    '        set_system_action_binding(action, group, -1, -1, 0, ""); \\\n',
    '        set_system_action_binding(action, group, -1, -1, 0, "!"); \\\n')
replace_once(
    "src/main/frontend/config.hpp",
    '        set_radio_binding(group, -1, -1, 0, ""); \\\n',
    '        set_radio_binding(group, -1, -1, 0, "!"); \\\n')


# ---------------------------------------------------------------------------
# Gamepad system actions: A/Cross = accept, B/Circle = back, R3 = pause.
# These are context-only defaults and are not aliases to gameplay actions.
# ---------------------------------------------------------------------------
replace_once(
    "src/main/sdl2/input.cpp",
'''    int default_system_gamepad_button(int action)
    {
        if (action == Config::SYSTEM_ACTION_ACCEPT)
            return SDL_CONTROLLER_BUTTON_A;
        if (action == Config::SYSTEM_ACTION_BACK)
            return SDL_CONTROLLER_BUTTON_B;
        return SDL_CONTROLLER_BUTTON_START;
    }
''',
'''    int default_system_gamepad_button(int action)
    {
        if (action == Config::SYSTEM_ACTION_ACCEPT)
            return SDL_CONTROLLER_BUTTON_A;
        if (action == Config::SYSTEM_ACTION_BACK)
            return SDL_CONTROLLER_BUTTON_B;
        return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    }
''')

replace_once(
    "src/main/sdl2/input.cpp",
'''        const std::string stored_device =
            config.system_action_binding_device(action, group);

        const bool has_custom_binding =
''',
'''        const std::string stored_device =
            config.system_action_binding_device(action, group);

        // "!" is an explicit unbound marker. Empty means no custom binding and
        // therefore allows the standard GAMEPAD fallback below.
        if (stored_device == "!")
            return -1;

        const bool has_custom_binding =
''')

# Keyboard system actions use their own configurable keys. They are contextual:
# pause only in-game, accept/back only in the frontend.
replace_once(
    "src/main/sdl2/input.cpp",
'''    if (keysym->sym == key_config[12]) set_key_state(VIEW1, true);
    if (keysym->sym == key_config[13]) set_key_state(VIEW2, true);
    if (keysym->sym == key_config[14]) set_key_state(VIEW3, true);

    // Permanent frontend confirm keys remain available independently from all
    // gameplay mappings. Escape remains handled by Menu::handle_escape().
    if (cannonball::state == cannonball::STATE_MENU &&
        (keysym->sym == SDLK_RETURN || keysym->sym == SDLK_KP_ENTER))
    {
        set_key_state(ACCEPT, true);
    }
''',
'''    if (keysym->sym == key_config[12]) set_key_state(VIEW1, true);
    if (keysym->sym == key_config[13]) set_key_state(VIEW2, true);
    if (keysym->sym == key_config[14]) set_key_state(VIEW3, true);

    if (cannonball::state == cannonball::STATE_GAME &&
        keysym->sym == config.system_action_key(Config::SYSTEM_ACTION_PAUSE))
    {
        set_key_state(PAUSE, true);
    }

    if (cannonball::state == cannonball::STATE_MENU)
    {
        if (keysym->sym == config.system_action_key(Config::SYSTEM_ACTION_ACCEPT))
            set_key_state(ACCEPT, true);
        if (keysym->sym == config.system_action_key(Config::SYSTEM_ACTION_BACK))
            set_key_state(BACK, true);
    }
''')

replace_once(
    "src/main/sdl2/input.cpp",
'''    if (keysym->sym == key_config[12]) set_key_state(VIEW1, false);
    if (keysym->sym == key_config[13]) set_key_state(VIEW2, false);
    if (keysym->sym == key_config[14]) set_key_state(VIEW3, false);

    if (keysym->sym == SDLK_RETURN || keysym->sym == SDLK_KP_ENTER)
        set_key_state(ACCEPT, false);
''',
'''    if (keysym->sym == key_config[12]) set_key_state(VIEW1, false);
    if (keysym->sym == key_config[13]) set_key_state(VIEW2, false);
    if (keysym->sym == key_config[14]) set_key_state(VIEW3, false);

    if (keysym->sym == config.system_action_key(Config::SYSTEM_ACTION_PAUSE))
        set_key_state(PAUSE, false);
    if (keysym->sym == config.system_action_key(Config::SYSTEM_ACTION_ACCEPT))
        set_key_state(ACCEPT, false);
    if (keysym->sym == config.system_action_key(Config::SYSTEM_ACTION_BACK))
        set_key_state(BACK, false);
''')


# ---------------------------------------------------------------------------
# Legacy hard-wired F1/F5 paths would defeat rebindability. Pause and Menu
# Access now come from the binding configuration instead.
# ---------------------------------------------------------------------------
replace_once(
    "src/main/sdl2/input_base.cpp",
'''        case SDLK_F1:
            set_key_state(PAUSE, is_pressed);
            break;

''',
'''        // Pause is a normal, configurable DX system action.

''')
replace_once(
    "src/main/sdl2/input_base.cpp",
'''        case SDLK_F5:
            set_key_state(MENU, is_pressed);
            break;

''',
'''        // Menu Access is a normal, configurable DX input action.

''')


# ---------------------------------------------------------------------------
# Main event loop: Escape is no longer a hard-wired frontend Back key. It is
# just the default value of the configurable Menu Back action.
# ---------------------------------------------------------------------------
replace_once(
    "src/main/main.cpp",
'''            case SDL_KEYDOWN:
                // Escape is reserved as BACK while the frontend is active.
                // The configured master-break key is allowed to quit only from
                // the actual game, so the menu can only be exited via EXIT.
                if (event.key.keysym.sym == SDLK_ESCAPE &&
                    cannonball::state == STATE_MENU)
                {
                    if (event.key.repeat == 0 && menu)
                        menu->handle_escape();
                }
                else if (event.key.keysym.sym == config.master_break_key &&
                         cannonball::state == STATE_GAME)
                {
                    cannonball::state = STATE_QUIT;
                }
                else
                {
                    input.handle_key_down(&event.key.keysym);
                }
                break;

            case SDL_KEYUP:
                // A menu Escape key-down is consumed above and therefore has
                // no matching logical key press to release.
                if (!(event.key.keysym.sym == SDLK_ESCAPE &&
                      cannonball::state == STATE_MENU))
                {
                    input.handle_key_up(&event.key.keysym);
                }
                break;
''',
'''            case SDL_KEYDOWN:
                // The master-break key remains a gameplay-only emergency quit.
                // Frontend Back is a normal configurable logical action.
                if (event.key.keysym.sym == config.master_break_key &&
                    cannonball::state == STATE_GAME)
                {
                    cannonball::state = STATE_QUIT;
                }
                else
                {
                    input.handle_key_down(&event.key.keysym);
                }
                break;

            case SDL_KEYUP:
                input.handle_key_up(&event.key.keysym);
                break;
''')


# ---------------------------------------------------------------------------
# New/fresh keyboard profile: mirror MAME's familiar P1 layout.
# Existing stored user values are left untouched. Newly introduced system keys
# receive their defaults when the setting itself is absent.
# ---------------------------------------------------------------------------
replace_once(
    "src/main/frontend/config.cpp",
'''    // CannonBall's original keyboard default for opening the menu is F5.
    // CannonBall-SE changed this to M. Keep existing user mappings untouched,
    // but restore F5 for new configs and configs where the menu key is absent.
    if (first_run || cfg.get_int("controls.keyconfig.menu", -1) == -1)
        controls.keyconfig[10] = SDLK_F5;
''',
'''    // Fresh CannonBall DX installs use a MAME-compatible keyboard layout.
    // Existing stored mappings always win; only genuinely absent settings are
    // filled so upgrading never rewrites a user's controls.
    const int KEY_SETTING_MISSING = -0x3fffffff;
    auto apply_keyboard_default = [&](const char* path, int slot, SDL_Keycode key)
    {
        if (first_run || cfg.get_int(path, KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
            controls.keyconfig[slot] = key;
    };

    apply_keyboard_default("controls.keyconfig.up",    0, SDLK_UP);
    apply_keyboard_default("controls.keyconfig.down",  1, SDLK_DOWN);
    apply_keyboard_default("controls.keyconfig.left",  2, SDLK_LEFT);
    apply_keyboard_default("controls.keyconfig.right", 3, SDLK_RIGHT);
    apply_keyboard_default("controls.keyconfig.acc",   4, SDLK_LCTRL);  // MAME Button 1
    apply_keyboard_default("controls.keyconfig.brake", 5, SDLK_LALT);   // MAME Button 2
    apply_keyboard_default("controls.keyconfig.gear1", 6, SDLK_SPACE);  // MAME Button 3
    apply_keyboard_default("controls.keyconfig.gear2", 7, SDLK_LSHIFT); // MAME Button 4
    apply_keyboard_default("controls.keyconfig.start", 8, SDLK_1);      // P1 Start
    apply_keyboard_default("controls.keyconfig.coin",  9, SDLK_5);      // Coin 1
    apply_keyboard_default("controls.keyconfig.menu", 10, SDLK_TAB);    // MAME UI menu
    apply_keyboard_default("controls.keyconfig.view", 11, SDLK_z);      // MAME Button 5

    if (first_run ||
        cfg.get_int("controls.radio.keyboard", KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
    {
        set_radio_key(SDLK_x); // MAME Button 6
    }

    if (first_run ||
        cfg.get_int("controls.system.pause.keyboard", KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
    {
        set_system_action_key(SYSTEM_ACTION_PAUSE, SDLK_p); // MAME Pause
    }
    if (first_run ||
        cfg.get_int("controls.system.accept.keyboard", KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
    {
        set_system_action_key(SYSTEM_ACTION_ACCEPT, SDLK_RETURN);
    }
    if (first_run ||
        cfg.get_int("controls.system.back.keyboard", KEY_SETTING_MISSING) == KEY_SETTING_MISSING)
    {
        set_system_action_key(SYSTEM_ACTION_BACK, SDLK_ESCAPE);
    }
''')

# Gamepad first-run profile: Select/Back = Coin, Guide/Home/PS = Menu Access.
replace_once(
    "src/main/frontend/config.cpp",
'''        controls.padconfig[4] = SDL_CONTROLLER_BUTTON_START;
        controls.padconfig[5] = SDL_CONTROLLER_BUTTON_B;      // Coin
        controls.padconfig[6] = SDL_CONTROLLER_BUTTON_BACK;   // Menu
        controls.padconfig[7] = SDL_CONTROLLER_BUTTON_Y;      // View
''',
'''        controls.padconfig[4] = SDL_CONTROLLER_BUTTON_START;
        controls.padconfig[5] = SDL_CONTROLLER_BUTTON_BACK;   // Coin / Select
        controls.padconfig[6] = SDL_CONTROLLER_BUTTON_GUIDE;  // Menu Access / Home / PS
        controls.padconfig[7] = SDL_CONTROLLER_BUTTON_Y;      // View
''')
replace_once(
    "src/main/frontend/config.cpp",
'''        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_COIN,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_B,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_MENU,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_BACK,
            device);
''',
'''        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_COIN,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_BACK,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_MENU,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_GUIDE,
            device);
''')


# ---------------------------------------------------------------------------
# Binding matrix: system-action keyboard cells are normal editable cells now.
# Raw Enter/Escape remain editor-navigation fallbacks so the editor cannot be
# stranded by a bad custom Accept/Back mapping.
# ---------------------------------------------------------------------------
replace_once(
    "src/main/frontend/menu.cpp",
'''    // Steering is a two-key cell and therefore uses -1 here. System actions
    // have fixed keyboard fallbacks and Radio owns a separate persistent key.
''',
'''    // Steering is a two-key cell and therefore uses -1 here. System actions
    // and Radio use their own persistent keyboard settings rather than slots.
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''            case SDLK_SPACE:     return "SPACE";
            case SDLK_ESCAPE:    return "ESC";
            case SDLK_F1:        return "F1";
''',
'''            case SDLK_SPACE:     return "SPACE";
            case SDLK_ESCAPE:    return "ESC";
            case SDLK_TAB:       return "TAB";
            case SDLK_LCTRL:     return "LCTRL";
            case SDLK_LALT:      return "LALT";
            case SDLK_LSHIFT:    return "LSHIFT";
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''        if (row == PAUSE_ROW)
            return "F1";
        if (row == ACCEPT_ROW)
            return "ENTER";
        if (row == MENU_BACK_ROW)
            return "ESC";

        if (row == RADIO_ROW)
''',
'''        if (is_system_action_row(row))
            return clip_text(
                compact_key_name(config.system_action_key(system_action_for_row(row))),
                8);

        if (row == RADIO_ROW)
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''        const int index = config.system_action_binding_index(action, group);
        const std::string device = config.system_action_binding_device(action, group);

        if (index >= 0 && !device.empty())
''',
'''        const int index = config.system_action_binding_index(action, group);
        const std::string device = config.system_action_binding_device(action, group);

        if (device == "!")
            return "-";

        if (index >= 0 && !device.empty())
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''            if (action == Config::SYSTEM_ACTION_ACCEPT)
                return "A";
            if (action == Config::SYSTEM_ACTION_BACK)
                return "B";
            return "START";
''',
'''            if (action == Config::SYSTEM_ACTION_ACCEPT)
                return "A";
            if (action == Config::SYSTEM_ACTION_BACK)
                return "B";
            return "R3";
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''        if (index < 0 || device.empty())
            return "-";

        device_binding_t binding;
''',
'''        if (device == "!")
            return "-";

        // L3 is the standard GAMEPAD radio button until the user explicitly
        // replaces or clears it. WHEEL deliberately has no implicit default.
        if (index < 0 || device.empty())
            return group == Input::BINDING_GAMEPAD ? "L3" : "-";

        device_binding_t binding;
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''            ohud.blit_text_new(1, STATUS_Y,     "ARROWS - SELECT   ENTER - CHANGE", ohud.GREY);
            ohud.blit_text_new(1, STATUS_Y + 1, "DEL/BSP - CLEAR", ohud.GREY);
            ohud.blit_text_new(1, STATUS_Y + 2, "F1/ENTER/ESC KEYBOARD FIXED", ohud.GREY);
''',
'''            ohud.blit_text_new(1, STATUS_Y,     "ARROWS - SELECT   ENTER - CHANGE", ohud.GREY);
            ohud.blit_text_new(1, STATUS_Y + 1, "DEL/BSP - CLEAR   ESC - BACK", ohud.GREY);
            ohud.blit_text_new(1, STATUS_Y + 2, "ALL ACTIONS CAN BE REBOUND", ohud.GREY);
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''            // Pause/Accept/Back deliberately keep fixed keyboard fallbacks.
            // Their KEYBOARD cells therefore never enter capture mode.
            if (is_system_action_row(selected_row))
            {
                capturing = false;
                draw_editor();
                return;
            }

''',
'''            // System actions are regular editable keyboard cells. Raw editor
            // navigation fallbacks are handled only while browsing, below.

''')

replace_once(
    "src/main/frontend/menu.cpp",
'''                else if (selected_row == RADIO_ROW)
                {
                    config.set_radio_key(captured_key);
                    capture_after_release = false;
                }
                else
''',
'''                else if (selected_row == RADIO_ROW)
                {
                    config.set_radio_key(captured_key);
                    capture_after_release = false;
                }
                else if (is_system_action_row(selected_row))
                {
                    config.set_system_action_key(
                        system_action_for_row(selected_row), captured_key);
                    capture_after_release = false;
                }
                else
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''    if (input.has_pressed(Input::BACK))
    {
        leave_editor();
        return;
    }
''',
'''    if (input.has_pressed(Input::BACK) || input.key_press == SDLK_ESCAPE)
    {
        leave_editor();
        return;
    }
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''            if (is_system_action_row(selected_row))
            {
                // F1 / Enter / Escape are permanent keyboard fallbacks.
                changed = false;
            }
            else if (selected_row == 0)
''',
'''            if (is_system_action_row(selected_row))
            {
                config.set_system_action_key(
                    system_action_for_row(selected_row), -1);
            }
            else if (selected_row == 0)
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''    const bool activate = select_pressed();
''',
'''    // Raw Enter is an editor-only safety fallback. It remains usable even if
    // the user clears or remaps Menu Accept, and it can still be captured once
    // the editor is actively listening to a KEYBOARD cell.
    const bool activate =
        select_pressed() ||
        input.key_press == SDLK_RETURN ||
        input.key_press == SDLK_KP_ENTER;
''')

replace_once(
    "src/main/frontend/menu.cpp",
'''        // The keyboard side of Pause/Accept/Back is intentionally fixed. The
        // GAMEPAD and WHEEL cells beside it remain freely rebindable.
        if (selected_col == COL_KEYBOARD && is_system_action_row(selected_row))
        {
            osoundint.queue_sound(sound::BEEP1);
            input.key_press = -1;
            draw_editor();
            return;
        }

''',
'''        // Every KEYBOARD / GAMEPAD / WHEEL cell is editable. Enter remains
        // only an editor-navigation fallback while this screen is in browse mode.

''')


# ---------------------------------------------------------------------------
# Radio: use L3 as the GAMEPAD default, but make explicit clear suppress it.
# ---------------------------------------------------------------------------
replace_once(
    "src/main/engine/radio_button.hpp",
'''        if (config.input_mode_is_gamepad())
            return physical_binding_pressed(BINDING_GAMEPAD);

        return physical_binding_pressed(BINDING_WHEEL);
''',
'''        if (config.input_mode_is_gamepad())
        {
            const std::string device = config.radio_binding_device(BINDING_GAMEPAD);
            const int index = config.radio_binding_index(BINDING_GAMEPAD);

            if (device == "!")
                return false;

            if (index >= 0 && !device.empty())
                return physical_binding_pressed(BINDING_GAMEPAD);

            // Standard SDL layout: L3 / left-stick click is Radio.
            return input.joy_button == SDL_CONTROLLER_BUTTON_LEFTSTICK &&
                input.is_gamepad_device(input.joy_button_device);
        }

        return physical_binding_pressed(BINDING_WHEEL);
''')


# Sanity checks for the intended layout.
checks = {
    "src/main/sdl2/input.cpp": [
        "SDL_CONTROLLER_BUTTON_RIGHTSTICK",
        "config.system_action_key(Config::SYSTEM_ACTION_PAUSE)",
        'stored_device == "!"',
    ],
    "src/main/frontend/config.cpp": [
        "SDLK_LCTRL",
        "SDLK_LALT",
        "SDLK_SPACE",
        "SDLK_LSHIFT",
        "SDLK_1",
        "SDLK_5",
        "SDLK_TAB",
        "SDL_CONTROLLER_BUTTON_GUIDE",
    ],
    "src/main/frontend/menu.cpp": [
        'return "R3";',
        'return group == Input::BINDING_GAMEPAD ? "L3" : "-";',
        "ALL ACTIONS CAN BE REBOUND",
    ],
    "src/main/engine/radio_button.hpp": [
        "SDL_CONTROLLER_BUTTON_LEFTSTICK",
    ],
}
for path, needles in checks.items():
    text = read(path)
    for needle in needles:
        if needle not in text:
            raise RuntimeError(f"{path}: sanity check failed for {needle!r}")

print("Input defaults refactor applied successfully")
