/***************************************************************************
    XML Configuration File Handling - CannonBall-SE extensions.

    The existing configuration implementation is retained in config_base.cpp.
    This wrapper adds persistent bindings for the three optional direct-view
    buttons and the per-device control-binding editor.
***************************************************************************/

// Pre-include the base file's dependencies before the temporary method-name
// macros below. This keeps the macros away from standard-library headers.
#include <iostream>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <regex>
#include <map>
#include <algorithm>
#include <cctype>
#include <string>
#include <cstdio>
#include <sstream>

#include "main.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "../utils.hpp"
#include "engine/car_palette_state.hpp"
#include "engine/ohiscore.hpp"
#include "engine/outils.hpp"
#include "engine/audio/osoundint.hpp"
#include "sdl2/gamepad_rumble_state.hpp"
#include "sdl2/pixel_scaler_state.hpp"

// Retain the existing Config::load/save implementation under private names.
#define load load_base
#define save save_base
#include "config_base.cpp"
#undef load
#undef save

namespace
{
    void add_legacy_binding(
        controls_settings_t& controls,
        int target,
        int type,
        int index,
        int value,
        const std::string& device)
    {
        if (index < 0)
            return;

        device_binding_t binding;
        binding.target = target;
        binding.type = type;
        binding.index = index;
        binding.value = value;
        binding.device = device.empty() ? "*" : device;
        controls.device_bindings.push_back(binding);
    }

    void migrate_legacy_device_bindings(controls_settings_t& controls)
    {
        // Analog controls already persisted a device signature, so those can
        // be migrated exactly. Old button bindings had no persisted device;
        // "*" deliberately preserves their previous any-device behaviour.
        add_legacy_binding(
            controls,
            device_binding_t::TARGET_STEER,
            device_binding_t::TYPE_AXIS,
            controls.axis[0],
            0,
            controls.axis_device[0]);

        add_legacy_binding(
            controls,
            device_binding_t::TARGET_ACCEL,
            device_binding_t::TYPE_AXIS,
            controls.axis[1],
            0,
            controls.axis_device[1]);

        add_legacy_binding(
            controls,
            device_binding_t::TARGET_BRAKE,
            device_binding_t::TYPE_AXIS,
            controls.axis[2],
            0,
            controls.axis_device[2]);

        static const int PAD_SLOT[] =
        {
            0,  // accelerate
            1,  // brake
            2,  // gear low / toggle
            3,  // gear high
            4,  // start
            5,  // coin
            6,  // menu
            7,  // view change
            15, // direct view 1
            16, // direct view 2
            17, // direct view 3
        };

        static const int TARGET[] =
        {
            device_binding_t::TARGET_ACCEL,
            device_binding_t::TARGET_BRAKE,
            device_binding_t::TARGET_GEAR1,
            device_binding_t::TARGET_GEAR2,
            device_binding_t::TARGET_START,
            device_binding_t::TARGET_COIN,
            device_binding_t::TARGET_MENU,
            device_binding_t::TARGET_VIEW,
            device_binding_t::TARGET_VIEW1,
            device_binding_t::TARGET_VIEW2,
            device_binding_t::TARGET_VIEW3,
        };

        for (int i = 0; i < 11; i++)
        {
            add_legacy_binding(
                controls,
                TARGET[i],
                device_binding_t::TYPE_BUTTON,
                controls.padconfig[PAD_SLOT[i]],
                0,
                "*");
        }
    }

    void disable_migrated_legacy_bindings(controls_settings_t& controls)
    {
        // Directional D-pad/HAT bindings (8-11) and cabinet motor limits
        // (12-14) stay in the original system for now. Everything represented
        // by the matrix is handled by device_bindings instead.
        controls.axis[0] = -1;
        controls.axis[1] = -1;
        controls.axis[2] = -1;
        controls.axis_device[0].clear();
        controls.axis_device[1].clear();
        controls.axis_device[2].clear();

        static const int PAD_SLOT[] =
        {
            0, 1, 2, 3, 4, 5, 6, 7, 15, 16, 17
        };

        for (int slot : PAD_SLOT)
            controls.padconfig[slot] = -1;
    }

    void apply_default_gamepad_legacy_bindings(controls_settings_t& controls)
    {
        // First-run controller profile. These are SDL GameController values,
        // so an Xbox 360/XInput-style pad works immediately without setup.
        controls.gear = controls_settings_t::GEAR_SEPARATE;
        controls.analog = 1;

        controls.axis[0] = SDL_CONTROLLER_AXIS_LEFTX;
        controls.axis[1] = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        controls.axis[2] = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        controls.axis_device[0].clear();
        controls.axis_device[1].clear();
        controls.axis_device[2].clear();
        controls.invert[1] = false;
        controls.invert[2] = false;
        controls.asettings[0] = 0;
        controls.asettings[1] = 0;

        // Triggers are the primary analog pedals. Shoulder buttons remain as
        // convenient digital fallbacks for controllers without usable axes.
        controls.padconfig[0] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        controls.padconfig[1] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        controls.padconfig[2] = SDL_CONTROLLER_BUTTON_A;      // Low / downshift
        controls.padconfig[3] = SDL_CONTROLLER_BUTTON_X;      // High / upshift
        controls.padconfig[4] = SDL_CONTROLLER_BUTTON_START;
        controls.padconfig[5] = SDL_CONTROLLER_BUTTON_B;      // Coin
        controls.padconfig[6] = SDL_CONTROLLER_BUTTON_BACK;   // Menu
        controls.padconfig[7] = SDL_CONTROLLER_BUTTON_Y;      // View

        // D-pad is also handled permanently by Input, but keep the legacy
        // values valid so the generated config is self-explanatory.
        controls.padconfig[8]  = SDL_CONTROLLER_BUTTON_DPAD_UP;
        controls.padconfig[9]  = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        controls.padconfig[10] = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        controls.padconfig[11] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    }

    bool binding_is_gamepad(const device_binding_t& binding)
    {
        return binding.device.size() >= 2 &&
            binding.device.compare(0, 2, "G:") == 0;
    }

    bool gamepad_target_is_bound(
        const controls_settings_t& controls,
        int target)
    {
        for (const auto& binding : controls.device_bindings)
        {
            if (binding.target == target && binding_is_gamepad(binding))
                return true;
        }

        return false;
    }

    void add_default_gamepad_binding(
        controls_settings_t& controls,
        int target,
        int type,
        int index,
        const std::string& device)
    {
        if (gamepad_target_is_bound(controls, target))
            return;

        device_binding_t binding;
        binding.target = target;
        binding.type = type;
        binding.index = index;
        binding.value = 0;
        binding.device = device;
        controls.device_bindings.push_back(binding);
    }

    std::string first_gamecontroller_signature()
    {
        if ((SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK) == 0)
            return std::string();

        const int count = SDL_NumJoysticks();

        for (int i = 0; i < count; i++)
        {
            if (!SDL_IsGameController(i))
                continue;

            SDL_Joystick* joystick = SDL_JoystickOpen(i);
            if (!joystick)
                continue;

            SDL_JoystickGUID guid = SDL_JoystickGetGUID(joystick);
            char guid_string[33] = {};
            SDL_JoystickGetGUIDString(guid, guid_string, sizeof(guid_string));

            const std::string signature =
                std::string(guid_string) +
                "|A" + std::to_string(SDL_JoystickNumAxes(joystick)) +
                "|B" + std::to_string(SDL_JoystickNumButtons(joystick)) +
                "|H" + std::to_string(SDL_JoystickNumHats(joystick));

            SDL_JoystickClose(joystick);
            return signature;
        }

        return std::string();
    }

    bool materialize_default_gamepad_bindings(controls_settings_t& controls)
    {
        const std::string signature = first_gamecontroller_signature();
        if (signature.empty())
            return false;

        const std::string device = "G:" + signature;

        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_STEER,
            device_binding_t::TYPE_AXIS,
            SDL_CONTROLLER_AXIS_LEFTX,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_ACCEL,
            device_binding_t::TYPE_AXIS,
            SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_BRAKE,
            device_binding_t::TYPE_AXIS,
            SDL_CONTROLLER_AXIS_TRIGGERLEFT,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_GEAR1,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_A,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_GEAR2,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_X,
            device);
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_START,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_START,
            device);
        add_default_gamepad_binding(
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
        add_default_gamepad_binding(
            controls,
            device_binding_t::TARGET_VIEW,
            device_binding_t::TYPE_BUTTON,
            SDL_CONTROLLER_BUTTON_Y,
            device);

        return true;
    }

    bool parse_device_bindings(
        const std::string& encoded,
        std::vector<device_binding_t>& bindings)
    {
        bindings.clear();

        if (encoded.empty())
            return false;

        std::stringstream entries(encoded);
        std::string entry;

        while (std::getline(entries, entry, ';'))
        {
            if (entry.empty())
                continue;

            std::stringstream fields(entry);
            std::string target;
            std::string type;
            std::string index;
            std::string value;
            std::string device;

            if (!std::getline(fields, target, ',') ||
                !std::getline(fields, type, ',') ||
                !std::getline(fields, index, ',') ||
                !std::getline(fields, value, ',') ||
                !std::getline(fields, device))
            {
                continue;
            }

            try
            {
                device_binding_t binding;
                binding.target = std::stoi(target);
                binding.type = std::stoi(type);
                binding.index = std::stoi(index);
                binding.value = std::stoi(value);
                binding.device = device;

                if (binding.target < device_binding_t::TARGET_STEER ||
                    binding.target > device_binding_t::TARGET_VIEW3 ||
                    binding.type < device_binding_t::TYPE_BUTTON ||
                    binding.type > device_binding_t::TYPE_HAT ||
                    binding.index < 0 ||
                    binding.device.empty())
                {
                    continue;
                }

                bindings.push_back(binding);
            }
            catch (...)
            {
                // Ignore malformed entries and keep loading the rest.
            }
        }

        return true;
    }

    std::string encode_device_bindings(
        const std::vector<device_binding_t>& bindings)
    {
        std::ostringstream encoded;
        bool first = true;

        for (const auto& binding : bindings)
        {
            if (binding.index < 0 || binding.device.empty())
                continue;

            if (!first)
                encoded << ';';

            first = false;
            encoded
                << binding.target << ','
                << binding.type << ','
                << binding.index << ','
                << binding.value << ','
                << binding.device;
        }

        return encoded.str();
    }

    bool parent_has_comment(
        tinyxml2::XMLNode* parent,
        const std::string& text)
    {
        if (!parent)
            return false;

        for (tinyxml2::XMLNode* node = parent->FirstChild();
             node;
             node = node->NextSibling())
        {
            if (const tinyxml2::XMLComment* comment = node->ToComment())
            {
                const char* value = comment->Value();
                if (value && text == value)
                    return true;
            }
        }

        return false;
    }

    void add_config_comment_before(
        xml_parser::ptree& cfg,
        const char* path,
        const char* text)
    {
        tinyxml2::XMLElement* element = cfg.find_node(path);
        if (!element || !element->Parent() || !text || !*text)
            return;

        tinyxml2::XMLNode* parent = element->Parent();
        if (parent_has_comment(parent, text))
            return;

        tinyxml2::XMLNode* comment = cfg.doc.NewComment(text);
        if (tinyxml2::XMLNode* previous = element->PreviousSibling())
            parent->InsertAfterChild(previous, comment);
        else
            parent->InsertFirstChild(comment);
    }

    void add_ffb_config_comments(xml_parser::ptree& cfg)
    {
        struct ConfigComment
        {
            const char* path;
            const char* text;
        };

        static const ConfigComment COMMENTS[] =
        {
            { "controls.analog.haptic.effects.sand",
              "FFB effect strengths use 0-100. 0 disables the individual effect; 100 is maximum configured strength." },
            { "controls.analog.haptic.effects.sand",
              "Sand / rough-surface grit taps." },
            { "controls.analog.haptic.effects.tyre_slip",
              "Tyre-slip vibration while sliding on the road." },
            { "controls.analog.haptic.effects.offroad_rumble_one_wheel",
              "Off-road vibration when one side of the car leaves the road." },
            { "controls.analog.haptic.effects.offroad_rumble_full",
              "Off-road vibration when the whole car is off the road." },
            { "controls.analog.haptic.effects.offroad_pull_one_wheel",
              "Outward steering pull when one side of the car is off-road." },
            { "controls.analog.haptic.effects.offroad_pull_full",
              "Outward steering pull when the whole car is off-road." },
            { "controls.analog.haptic.effects.gear_shift",
              "Gear-change kick and rebound." },
            { "controls.analog.haptic.effects.music_selector",
              "Short Music Select step impulse between songs." },
            { "controls.analog.haptic.effects.traffic_skid",
              "Steering yank after a traffic collision / skid." },
            { "controls.analog.haptic.effects.crash_bump",
              "Low-speed scenery impact." },
            { "controls.analog.haptic.effects.crash_spin_impact",
              "Initial impact that starts a scenery spin." },
            { "controls.analog.haptic.effects.crash_spin",
              "Repeated side loads while the car spins." },
            { "controls.analog.haptic.effects.crash_flip_impact",
              "Initial impact that starts a high-speed flip." },
            { "controls.analog.haptic.effects.crash_flip",
              "Repeated / sustained side loads during a flip." },
            { "controls.analog.haptic.effects.crash_flip_landing",
              "Landing impact after a flip." },
            { "controls.analog.haptic.effects.start_steering",
              "Automatic steering load while the Ferrari drives onto the start line." },
            { "controls.analog.haptic.effects.start_rev_shake",
              "Throttle-dependent engine / rev shake before the start." },

            { "controls.analog.haptic.spring.low_speed",
              "Spring strength percentages use 0-100 and are relative to the Spring value selected in the in-game Controls menu." },
            { "controls.analog.haptic.spring.low_speed",
              "Low-speed spring percentage used in menus, Attract Mode, stationary driving and low speed." },
            { "controls.analog.haptic.spring.high_speed",
              "High-speed spring percentage reached at speed_full." },
            { "controls.analog.haptic.spring.sliding",
              "Percentage of the currently active spring retained during on-road tyre slip." },
            { "controls.analog.haptic.spring.speed_start",
              "Vehicle-speed threshold, NOT a percentage. Valid range 0-294; spring starts increasing here." },
            { "controls.analog.haptic.spring.speed_full",
              "Vehicle-speed threshold, NOT a percentage. Valid range 0-294; spring reaches high_speed here and stays there up to the normal maximum speed of 294." },
            { "controls.analog.haptic.spring.traffic_skid",
              "Spring percentage during a traffic-collision skid." },
            { "controls.analog.haptic.spring.crash_bump",
              "Spring percentage during a low-speed scenery bump." },
            { "controls.analog.haptic.spring.crash_spin",
              "Spring percentage during the active scenery spin." },
            { "controls.analog.haptic.spring.crash_recovery",
              "Spring percentage during spin recovery." },
            { "controls.analog.haptic.spring.crash_flip_start",
              "Spring percentage at the start of a flip." },
            { "controls.analog.haptic.spring.crash_flip_airborne",
              "Spring percentage while the car is airborne." },
            { "controls.analog.haptic.spring.crash_flip_transition",
              "Spring percentage through the flip transition." },
            { "controls.analog.haptic.spring.crash_flip_landing",
              "Spring percentage at landing." },
            { "controls.analog.haptic.spring.crash_flip_recovery",
              "Spring percentage during post-flip recovery." },
        };

        for (const ConfigComment& entry : COMMENTS)
            add_config_comment_before(cfg, entry.path, entry.text);
    }
}

void Config::load()
{
    const bool first_run = !std::filesystem::exists(data.cfg_file);

    load_base();

    // CannonBall's original keyboard default for opening the menu is F5.
    // CannonBall-SE changed this to M. Keep existing user mappings untouched,
    // but restore F5 for new configs and configs where the menu key is absent.
    if (first_run || cfg.get_int("controls.keyconfig.menu", -1) == -1)
        controls.keyconfig[10] = SDLK_F5;

    // CannonBall DX stores every score table in one physical file. Keep the
    // old mode-specific paths as logical selectors, but make Original World
    // point at the real file so the legacy Clear Scores action removes it too.
    data.file_scores = data.save_path + "highscores.xml";
    xml_parser::migrate_highscores(data.save_path);

    // Do not inherit historical CannonBall-SE CRT/filter defaults on first run.
    // This deliberately overrides both the hard-coded config_base fallbacks and
    // any outdated resource config that may still be present in a build folder.
    if (first_run)
    {
        video.widescreen = 0;
        video.fps = 2;
        video.shader_mode = 0;
        video.shadow_mask = 0;
        video.maskDim = 100;
        video.maskBoost = 100;
        video.scanlines = 0;
        video.crt_shape = 0;
        video.vignette = 0;
        video.noise = 0;
        video.warpX = 0;
        video.warpY = 0;
        video.desaturate = 0;
        video.desaturate_edges = 0;
        video.brightboost = 0;
        video.blargg = 0;
        video.saturation = 0;
        video.contrast = 0;
        video.brightness = 0;
        video.sharpness = 0;
        video.resolution = 0;
        video.gamma = 0;
        video.hue = 0;

        sound.playback_device = -1;

        // Canonical CannonBall DX wheel defaults. These match the tested
        // headroom preset materialized below by seed_ffb_tuning_defaults().
        controls.haptic = 1;
        controls.ffb_strength = 50;
        controls.centering_strength = 60;

        // Ensure an old resource config cannot leak previous FFB values into
        // a freshly generated config.xml.
        cfg.erase("controls.analog.haptic.effects");
        cfg.erase("controls.analog.haptic.spring");
        cfg.put_int("controls.analog.haptic.<xmlattr>.enabled", 1);
        cfg.put_int("controls.analog.haptic.strength", 50);
        cfg.put_int("controls.analog.haptic.centering_strength", 60);

        // Mark the standard Xbox/SDL profile even if an old resource config
        // was loaded. It will be materialized to the physical pad after SDL
        // has initialized and the config is next saved.
        cfg.put_int("controls.default_gamepad", 1);
        apply_default_gamepad_legacy_bindings(controls);
    }
    else if (cfg.get_int("controls.analog.haptic.centering_strength", -1) == -1)
    {
        // Existing configs that never had the basic Spring value use the new
        // CannonBall DX default without changing any explicitly stored value.
        controls.centering_strength = 60;
    }

    // engine.car_pal remains the live/runtime Ferrari colour. Keep a separate
    // persistent default so Music Select can change the race colour without
    // ever overwriting the user's attract/default colour.
    car_palette_state::initialize(engine.car_pal);
    engine.car_pal = car_palette_state::get_default(engine.car_pal);

    int scaler_mode = cfg.get_int("video.pixel_scaler", pixel_scaler::OFF);
    int scaler_last = cfg.get_int("video.pixel_scaler_last", pixel_scaler::XBRZ_4X);

    if (first_run)
        scaler_mode = pixel_scaler::OFF;

    if (!pixel_scaler::valid(scaler_mode))
        scaler_mode = pixel_scaler::OFF;
    if (!pixel_scaler::active(scaler_last))
        scaler_last = pixel_scaler::XBRZ_4X;

    pixel_scaler::last_mode.store(scaler_last, std::memory_order_relaxed);
    pixel_scaler::set(scaler_mode);

    // Rumble enable is deliberately independent from rumble strength. Legacy
    // configs used strength 0 as OFF, so preserve that intent on first load.
    const bool legacy_rumble_enabled = controls.rumble > 0.0f;
    gamepad_rumble::enabled =
        cfg.get_int(
            "controls.rumble_enabled",
            legacy_rumble_enabled ? 1 : 0) != 0;

    // The old default could exceed the menu's 0..1 range. Keep a valid stored
    // strength even while rumble is disabled; the separate enable flag decides
    // whether the motors actually run.
    if (controls.rumble <= 0.0f)
        controls.rumble = 0.5f;
    else if (controls.rumble > 1.0f)
        controls.rumble = 1.0f;

    // Optional direct camera selection bindings. -1 means unassigned.
    controls.keyconfig[12] = cfg.get_int("controls.keyconfig.view1", -1);
    controls.keyconfig[13] = cfg.get_int("controls.keyconfig.view2", -1);
    controls.keyconfig[14] = cfg.get_int("controls.keyconfig.view3", -1);

    // Slots 12-14 remain the original cabinet motor-limit inputs.
    // The three new view buttons therefore use slots 15-17.
    controls.padconfig[15] = cfg.get_int("controls.padconfig.view1", -1);
    controls.padconfig[16] = cfg.get_int("controls.padconfig.view2", -1);
    controls.padconfig[17] = cfg.get_int("controls.padconfig.view3", -1);

    const std::string encoded =
        cfg.get_string("controls.device_bindings", "");
    const bool default_gamepad_profile =
        cfg.get_int("controls.default_gamepad", 0) != 0;
    const bool parsed_device_bindings =
        parse_device_bindings(encoded, controls.device_bindings);

    if (default_gamepad_profile && !parsed_device_bindings)
    {
        // A freshly generated config starts with a standard SDL/XInput pad
        // profile. Keep it in the legacy arrays until SDL is initialized; the
        // first normal config save converts it to persistent device bindings.
        controls.device_bindings.clear();
        apply_default_gamepad_legacy_bindings(controls);
    }
    else
    {
        if (!parsed_device_bindings)
            migrate_legacy_device_bindings(controls);

        disable_migrated_legacy_bindings(controls);
    }

    // load_base() may already have created config.xml using its historical
    // defaults. Rewrite it now with the canonical first-run profile above.
    if (first_run)
        save();
}

bool Config::save()
{
    cfg.put_int(
        "video.pixel_scaler",
        pixel_scaler::mode.load(std::memory_order_relaxed));
    cfg.put_int(
        "video.pixel_scaler_last",
        pixel_scaler::last_mode.load(std::memory_order_relaxed));

    // Keep the on/off state separate so switching rumble off never overwrites
    // the user's preferred intensity.
    cfg.put_int(
        "controls.rumble_enabled",
        gamepad_rumble::enabled ? 1 : 0);

    // Add the direct-view keyboard bindings to the same config tree before the
    // existing save routine writes it.
    cfg.put_int("controls.keyconfig.view1", controls.keyconfig[12]);
    cfg.put_int("controls.keyconfig.view2", controls.keyconfig[13]);
    cfg.put_int("controls.keyconfig.view3", controls.keyconfig[14]);

    // Once SDL is running, turn the first-run generic Xbox profile into the
    // same physical-device bindings used by the binding matrix. Existing user
    // assignments win target-by-target; missing cells receive the defaults.
    if (cfg.get_int("controls.default_gamepad", 0) != 0 &&
        materialize_default_gamepad_bindings(controls))
    {
        disable_migrated_legacy_bindings(controls);
        cfg.erase("controls.default_gamepad");
    }

    // Per-device bindings supersede the old single pad/axis assignment for all
    // controls represented by the matrix.
    cfg.put_string(
        "controls.device_bindings",
        encode_device_bindings(controls.device_bindings));

    // Always materialize the current DX wheel-tuning defaults in the generated
    // config, even when haptics are disabled on first launch. Then attach the
    // English documentation directly to the real XML tree that save_base()
    // writes to config.xml.
    seed_ffb_tuning_defaults();
    add_ffb_config_comments(cfg);

    // A colour changed in the frontend settings menu is a real default change.
    // While the engine is running, car_pal is runtime state: Music Select and
    // the race are never allowed to promote that temporary value implicitly.
    if (cannonball::state != cannonball::STATE_GAME)
        car_palette_state::set_default(engine.car_pal);

    // config_base.cpp persists engine.car_pal as engine.car_color. Temporarily
    // substitute the persistent attract/default colour so a Music Select or
    // in-race colour can never leak into config.xml.
    const int runtime_car_pal = engine.car_pal;
    engine.car_pal = car_palette_state::get_default(runtime_car_pal);
    const bool saved = save_base();
    engine.car_pal = runtime_car_pal;

    return saved;
}