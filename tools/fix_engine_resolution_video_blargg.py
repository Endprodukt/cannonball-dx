from pathlib import Path


def replace_once(text, old, new, label):
    if new in text:
        print(f"{label}: already applied")
        return text, False
    if old not in text:
        raise SystemExit(f"{label}: expected pattern not found")
    print(f"{label}: applied")
    return text.replace(old, new, 1), True


# ---------------------------------------------------------------------------
# Blargg: allow every high-resolution engine mode to use the existing hires
# NTSC blitter. The blitter consumes the actual input width; there is no need
# to disable it merely because the engine scale is 3x or 4x.
# ---------------------------------------------------------------------------
path = Path("src/main/sdl2/rendersurface.cpp")
text = path.read_text(encoding="utf-8")

bypass = '''    const int render_scale = std::clamp(config.video.hires + 1, 1, 4);\n    if (blargg && render_scale > 2)\n    {\n        std::cout\n            << "Blargg filter bypassed at " << render_scale\n            << "x engine resolution (Blargg hi-res input is fixed at 2x)."\n            << std::endl;\n        blargg = video_settings_t::BLARGG_DISABLE;\n    }\n\n'''

if bypass in text:
    text = text.replace(bypass, "", 1)
    path.write_text(text, encoding="utf-8")
    print("Blargg: enabled existing hires path for 3x/4x")
elif "Blargg filter bypassed at " in text:
    raise SystemExit("Blargg: unexpected bypass implementation")
else:
    print("Blargg: 3x/4x bypass already removed")


# ---------------------------------------------------------------------------
# Menu layout: Engine Resolution is a fundamental video setting, so place it
# in VIDEO directly after aspect ratio and remove it from ENHANCEMENTS.
# ---------------------------------------------------------------------------
path = Path("src/main/frontend/menu_legacy.hpp.inc")
text = path.read_text(encoding="utf-8")
changed = False

text, did = replace_once(
    text,
    '''            menu_video.push_back(ENTRY_WIDESCREEN);\n            menu_video.push_back(ENTRY_FRAME_RATE);\n''',
    '''            menu_video.push_back(ENTRY_WIDESCREEN);\n            menu_video.push_back(ENTRY_HIRES);\n            menu_video.push_back(ENTRY_FRAME_RATE);\n''',
    "Menu layout: add Engine Resolution to VIDEO")
changed |= did

text, did = replace_once(
    text,
    '''            menu_enhancements.clear();\n            menu_enhancements.push_back(ENTRY_HIRES);\n            menu_enhancements.push_back(ENTRY_SPRITERES);\n''',
    '''            menu_enhancements.clear();\n            menu_enhancements.push_back(ENTRY_SPRITERES);\n''',
    "Menu layout: remove Engine Resolution from ENHANCEMENTS")
changed |= did

if changed:
    path.write_text(text, encoding="utf-8")


# ---------------------------------------------------------------------------
# DX menu wrapper: convert and operate the four-step Engine Resolution entry
# in VIDEO rather than ENHANCEMENTS. Keeping the custom ENGINE RESOLUTION
# prefix prevents the inherited binary HI-RES toggle from seeing the row.
# ---------------------------------------------------------------------------
path = Path("src/main/frontend/menu.cpp")
text = path.read_text(encoding="utf-8")
changed = False

old_sync = '''    // Replace the inherited binary ORIGINAL/HI-RES entry with a DX multi-step\n    // render scale. A different prefix prevents MenuBase from applying its old\n    // XOR toggle when this row is activated.\n    if (!menu_enhancements.empty())\n    {\n        auto resolution_entry = std::find_if(\n            menu_enhancements.begin(),\n            menu_enhancements.end(),\n            [](const std::string& entry)\n            {\n                return starts_with_label(entry, ENTRY_HIRES) ||\n                       starts_with_label(entry, ENGINE_RESOLUTION_LABEL);\n            });\n\n        if (resolution_entry != menu_enhancements.end())\n            *resolution_entry = engine_resolution_menu_text();\n    }\n'''
new_sync = '''    // Replace the inherited binary ORIGINAL/HI-RES entry in VIDEO with the DX\n    // four-step render scale. A different prefix prevents MenuBase from applying\n    // its old XOR toggle when this row is activated.\n    if (!menu_video.empty())\n    {\n        auto resolution_entry = std::find_if(\n            menu_video.begin(),\n            menu_video.end(),\n            [](const std::string& entry)\n            {\n                return starts_with_label(entry, ENTRY_HIRES) ||\n                       starts_with_label(entry, ENGINE_RESOLUTION_LABEL);\n            });\n\n        if (resolution_entry != menu_video.end())\n            *resolution_entry = engine_resolution_menu_text();\n    }\n'''
text, did = replace_once(text, old_sync, new_sync, "Menu runtime: sync Engine Resolution in VIDEO")
changed |= did

old_directional = '''    // Engine resolution is a four-step value. Use the existing preserve-state\n    // video restart so changing it in the menu does not reset the running S16 state.\n    if (state == STATE_MENU &&\n        menu_selected == &menu_enhancements &&\n        cursor >= 0 &&\n        cursor < static_cast<int>(menu_enhancements.size()) &&\n        starts_with_label(menu_enhancements[cursor], ENGINE_RESOLUTION_LABEL) &&\n        (input.has_pressed(Input::LEFT) || input.has_pressed(Input::RIGHT)))\n    {\n        int scale = engine_resolution_scale();\n        if (input.has_pressed(Input::RIGHT))\n            scale = scale == 4 ? 1 : scale + 1;\n        else\n            scale = scale == 1 ? 4 : scale - 1;\n\n        request_engine_resolution_scale(scale);\n        menu_enhancements[cursor] = engine_resolution_menu_text(scale);\n        config_save_pending = true;\n        osoundint.queue_sound(sound::BEEP1);\n    }\n'''
new_directional = '''    // Engine resolution is a four-step VIDEO value. Use the existing preserve-state\n    // video restart so changing it in the menu does not reset the running S16 state.\n    if (state == STATE_MENU &&\n        menu_selected == &menu_video &&\n        cursor >= 0 &&\n        cursor < static_cast<int>(menu_video.size()) &&\n        starts_with_label(menu_video[cursor], ENGINE_RESOLUTION_LABEL) &&\n        (input.has_pressed(Input::LEFT) || input.has_pressed(Input::RIGHT)))\n    {\n        int scale = engine_resolution_scale();\n        if (input.has_pressed(Input::RIGHT))\n            scale = scale == 4 ? 1 : scale + 1;\n        else\n            scale = scale == 1 ? 4 : scale - 1;\n\n        request_engine_resolution_scale(scale);\n        menu_video[cursor] = engine_resolution_menu_text(scale);\n        config_save_pending = true;\n        osoundint.queue_sound(sound::BEEP1);\n    }\n'''
text, did = replace_once(text, old_directional, new_directional, "Menu input: LEFT/RIGHT Engine Resolution in VIDEO")
changed |= did

# Enter/Select fallback. Insert a VIDEO handler immediately before the existing
# ENHANCEMENTS handler, then remove the obsolete resolution case there.
video_select = '''    if (menu_selected == &menu_video &&\n        cursor >= 0 &&\n        cursor < static_cast<int>(menu_video.size()))\n    {\n        const std::string& option = menu_video[cursor];\n\n        if (starts_with_label(option, ENGINE_RESOLUTION_LABEL))\n        {\n            int scale = engine_resolution_scale() + 1;\n            if (scale > 4)\n                scale = 1;\n\n            request_engine_resolution_scale(scale);\n            menu_video[cursor] = engine_resolution_menu_text(scale);\n            return false;\n        }\n    }\n\n'''
anchor = '''    if (menu_selected == &menu_enhancements &&\n        cursor >= 0 &&\n        cursor < static_cast<int>(menu_enhancements.size()))\n    {\n        const std::string& option = menu_enhancements[cursor];\n'''
if video_select not in text:
    if anchor not in text:
        raise SystemExit("Menu input: ENHANCEMENTS select handler anchor not found")
    text = text.replace(anchor, video_select + anchor, 1)
    changed = True
    print("Menu input: Enter Engine Resolution in VIDEO: applied")
else:
    print("Menu input: Enter Engine Resolution in VIDEO: already applied")

obsolete = '''\n        if (starts_with_label(option, ENGINE_RESOLUTION_LABEL))\n        {\n            int scale = engine_resolution_scale() + 1;\n            if (scale > 4)\n                scale = 1;\n\n            request_engine_resolution_scale(scale);\n            menu_enhancements[cursor] = engine_resolution_menu_text(scale);\n            return false;\n        }\n'''
if obsolete in text:
    text = text.replace(obsolete, "", 1)
    changed = True
    print("Menu input: removed obsolete ENHANCEMENTS Engine Resolution handler")
else:
    print("Menu input: obsolete ENHANCEMENTS handler already absent")

if changed:
    path.write_text(text, encoding="utf-8")
