/***************************************************************************
    CannonBall DX configuration accessors.

    Runtime configuration loading and persistence remain in config.cpp.
    This file keeps the public header focused on the configuration API.
***************************************************************************/

#include "frontend/config.hpp"

int Config::input_mode()
{
    const int stored = cfg.get_int("controls.input_mode", -1);
    if (stored == INPUT_GAMEPAD || stored == INPUT_WHEEL)
        return stored;

    // Migration for configs created before INPUT MODE existed. Existing
    // wheel users generally already have a W: binding; otherwise prefer
    // GAMEPAD so a standard controller remains immediately usable.
    for (const auto& binding : controls.device_bindings)
    {
        if (binding.device.size() >= 2 &&
            binding.device.compare(0, 2, "W:") == 0)
        {
            return INPUT_WHEEL;
        }
    }

    return INPUT_GAMEPAD;
}

bool Config::input_mode_is_gamepad()
{
    return input_mode() == INPUT_GAMEPAD;
}

bool Config::input_mode_is_wheel()
{
    return input_mode() == INPUT_WHEEL;
}

void Config::set_input_mode(int mode)
{
    if (mode != INPUT_WHEEL)
        mode = INPUT_GAMEPAD;

    cfg.put_int("controls.input_mode", mode);
}

void Config::cycle_input_mode()
{
    set_input_mode(
        input_mode() == INPUT_WHEEL
            ? INPUT_GAMEPAD
            : INPUT_WHEEL);
}

int Config::ffb_effect_setting(const char* name, int default_value)
{
    const std::string setting = name ? name : "";
    int canonical_default = default_value;

    if      (setting == "sand")                     canonical_default = 3;
    else if (setting == "tyre_slip")                canonical_default = 11;
    else if (setting == "offroad_rumble_one_wheel") canonical_default = 20;
    else if (setting == "offroad_rumble_full")      canonical_default = 30;
    else if (setting == "offroad_pull_one_wheel")   canonical_default = 35;
    else if (setting == "offroad_pull_full")        canonical_default = 21;
    else if (setting == "gear_shift")               canonical_default = 49;
    else if (setting == "music_selector")           canonical_default = 7;
    else if (setting == "traffic_skid")             canonical_default = 70;
    else if (setting == "crash_bump")               canonical_default = 70;
    else if (setting == "crash_spin_impact")        canonical_default = 70;
    else if (setting == "crash_spin")               canonical_default = 70;
    else if (setting == "crash_flip_impact")        canonical_default = 70;
    else if (setting == "crash_flip")               canonical_default = 70;
    else if (setting == "crash_flip_landing")       canonical_default = 70;
    else if (setting == "start_steering")           canonical_default = 70;
    else if (setting == "start_rev_shake")          canonical_default = 30;

    const std::string path =
        std::string("controls.analog.haptic.effects.") + setting;
    int value = cfg.get_int(path, canonical_default);
    if (value < 0)
        return 0;
    if (value > 100)
        return 100;
    return value;
}

void Config::set_ffb_effect_setting(const char* name, int value)
{
    const std::string setting = name ? name : "";
    if (value < 0)
        value = 0;
    else if (value > 100)
        value = 100;

    cfg.put_int(
        std::string("controls.analog.haptic.effects.") + setting,
        value);
}

int Config::ffb_spring_setting(const char* name, int default_value)
{
    const std::string setting = name ? name : "";
    int canonical_default = default_value;

    if      (setting == "low_speed")             canonical_default = 28;
    else if (setting == "high_speed")            canonical_default = 70;
    else if (setting == "sliding")               canonical_default = 47;
    else if (setting == "speed_start")           canonical_default = 100;
    else if (setting == "speed_full")            canonical_default = 240;
    else if (setting == "traffic_skid")          canonical_default = 35;
    else if (setting == "crash_bump")            canonical_default = 46;
    else if (setting == "crash_spin")            canonical_default = 25;
    else if (setting == "crash_recovery")        canonical_default = 49;
    else if (setting == "crash_flip_start")      canonical_default = 32;
    else if (setting == "crash_flip_airborne")   canonical_default = 7;
    else if (setting == "crash_flip_transition") canonical_default = 18;
    else if (setting == "crash_flip_landing")    canonical_default = 32;
    else if (setting == "crash_flip_recovery")   canonical_default = 49;

    const std::string path =
        std::string("controls.analog.haptic.spring.") + setting;
    int value = cfg.get_int(path, canonical_default);
    const int maximum =
        (setting == "speed_start" || setting == "speed_full") ? 294 : 100;

    if (value < 0)
        return 0;
    if (value > maximum)
        return maximum;
    return value;
}

void Config::set_ffb_spring_setting(const char* name, int value)
{
    const std::string setting = name ? name : "";
    const int maximum =
        (setting == "speed_start" || setting == "speed_full") ? 294 : 100;

    if (value < 0)
        value = 0;
    else if (value > maximum)
        value = maximum;

    cfg.put_int(
        std::string("controls.analog.haptic.spring.") + setting,
        value);
}

void Config::seed_ffb_tuning_defaults()
{
    if (cfg.get_int("controls.analog.haptic.<xmlattr>.enabled", -1) < 0)
    {
        controls.haptic = 1;
        cfg.put_int("controls.analog.haptic.<xmlattr>.enabled", 1);
    }

    if (cfg.get_int("controls.analog.haptic.strength", -1) < 0)
    {
        controls.ffb_strength = 50;
        cfg.put_int("controls.analog.haptic.strength", 50);
    }

    if (cfg.get_int("controls.analog.haptic.centering_strength", -1) < 0)
    {
        controls.centering_strength = 60;
        cfg.put_int("controls.analog.haptic.centering_strength", 60);
    }

    auto seed_effect = [&](const char* name, int default_value)
    {
        const std::string path =
            std::string("controls.analog.haptic.effects.") + name;
        cfg.put_int(path, ffb_effect_setting(name, default_value));
    };

    auto seed_spring = [&](const char* name, int default_value)
    {
        const std::string path =
            std::string("controls.analog.haptic.spring.") + name;
        cfg.put_int(path, ffb_spring_setting(name, default_value));
    };

    seed_effect("sand", 3);
    seed_effect("tyre_slip", 11);
    seed_effect("offroad_rumble_one_wheel", 20);
    seed_effect("offroad_rumble_full", 30);
    seed_effect("offroad_pull_one_wheel", 35);
    seed_effect("offroad_pull_full", 21);
    seed_effect("gear_shift", 49);
    seed_effect("music_selector", 7);
    seed_effect("traffic_skid", 70);
    seed_effect("crash_bump", 70);
    seed_effect("crash_spin_impact", 70);
    seed_effect("crash_spin", 70);
    seed_effect("crash_flip_impact", 70);
    seed_effect("crash_flip", 70);
    seed_effect("crash_flip_landing", 70);
    seed_effect("start_steering", 70);
    seed_effect("start_rev_shake", 30);

    seed_spring("low_speed", 28);
    seed_spring("high_speed", 70);
    seed_spring("sliding", 47);
    seed_spring("speed_start", 100);
    seed_spring("speed_full", 240);
    seed_spring("traffic_skid", 35);
    seed_spring("crash_bump", 46);
    seed_spring("crash_spin", 25);
    seed_spring("crash_recovery", 49);
    seed_spring("crash_flip_start", 32);
    seed_spring("crash_flip_airborne", 7);
    seed_spring("crash_flip_transition", 18);
    seed_spring("crash_flip_landing", 32);
    seed_spring("crash_flip_recovery", 49);
}

int Config::selection_timer_seconds()
{
    const int value = cfg.get_int("engine.selection_timers", 30);
    if (value == 1)
        return 30;
    if (value == 15 || value == 30)
        return value;
    return 0;
}

bool Config::selection_timers_enabled()
{
    return selection_timer_seconds() != 0;
}

void Config::set_selection_timer_seconds(int seconds)
{
    if (seconds != 15 && seconds != 30)
        seconds = 0;
    cfg.put_int("engine.selection_timers", seconds);
}

void Config::cycle_selection_timer()
{
    const int seconds = selection_timer_seconds();
    if (seconds == 15)
        set_selection_timer_seconds(30);
    else if (seconds == 30)
        set_selection_timer_seconds(0);
    else
        set_selection_timer_seconds(15);
}

const char* Config::system_action_name(int action)
{
    if (action == SYSTEM_ACTION_ACCEPT) return "accept";
    if (action == SYSTEM_ACTION_BACK) return "back";
    return "pause";
}

int Config::system_action_key(int action)
{
    const std::string path = std::string("controls.system.") + system_action_name(action) + ".keyboard";
    return cfg.get_int(path, -1);
}

void Config::set_system_action_key(int action, int key)
{
    const std::string path = std::string("controls.system.") + system_action_name(action) + ".keyboard";
    cfg.put_int(path, key);
}

std::string Config::system_action_group_path(int action, int group, const char* leaf)
{
    return std::string("controls.system.") + system_action_name(action) +
        (group == 0 ? ".gamepad." : ".wheel.") + leaf;
}

int Config::system_action_binding_type(int action, int group)
{
    return cfg.get_int(system_action_group_path(action, group, "type"), -1);
}

int Config::system_action_binding_index(int action, int group)
{
    return cfg.get_int(system_action_group_path(action, group, "index"), -1);
}

int Config::system_action_binding_value(int action, int group)
{
    return cfg.get_int(system_action_group_path(action, group, "value"), 0);
}

std::string Config::system_action_binding_device(int action, int group)
{
    return cfg.get_string(system_action_group_path(action, group, "device"), "");
}

void Config::set_system_action_binding(int action, int group, int type, int index, int value, const std::string& device)
{
    cfg.put_int(system_action_group_path(action, group, "type"), type);
    cfg.put_int(system_action_group_path(action, group, "index"), index);
    cfg.put_int(system_action_group_path(action, group, "value"), value);
    cfg.put_string(system_action_group_path(action, group, "device"), device);
}

void Config::clear_system_action_binding(int action, int group)
{
    set_system_action_binding(action, group, -1, -1, 0, "!");
}

int Config::radio_key()
{
    return cfg.get_int("controls.radio.keyboard", -1);
}

void Config::set_radio_key(int key)
{
    cfg.put_int("controls.radio.keyboard", key);
}

int Config::radio_binding_type(int group)
{
    return cfg.get_int(
        group == 0 ? "controls.radio.gamepad.type" : "controls.radio.wheel.type",
        -1);
}

int Config::radio_binding_index(int group)
{
    return cfg.get_int(
        group == 0 ? "controls.radio.gamepad.index" : "controls.radio.wheel.index",
        -1);
}

int Config::radio_binding_value(int group)
{
    return cfg.get_int(
        group == 0 ? "controls.radio.gamepad.value" : "controls.radio.wheel.value",
        0);
}

std::string Config::radio_binding_device(int group)
{
    return cfg.get_string(
        group == 0 ? "controls.radio.gamepad.device" : "controls.radio.wheel.device",
        "");
}

void Config::set_radio_binding(int group, int type, int index, int value, const std::string& device)
{
    const char* type_path = group == 0 ? "controls.radio.gamepad.type" : "controls.radio.wheel.type";
    const char* index_path = group == 0 ? "controls.radio.gamepad.index" : "controls.radio.wheel.index";
    const char* value_path = group == 0 ? "controls.radio.gamepad.value" : "controls.radio.wheel.value";
    const char* device_path = group == 0 ? "controls.radio.gamepad.device" : "controls.radio.wheel.device";
    cfg.put_int(type_path, type);
    cfg.put_int(index_path, index);
    cfg.put_int(value_path, value);
    cfg.put_string(device_path, device);
}

void Config::clear_radio_binding(int group)
{
    set_radio_binding(group, -1, -1, 0, "!");
}

int Config::engine_vibration_strength()
{
    int value = cfg.get_int(
        "controls.analog.haptic.engine_vibration_strength",
        ENGINE_VIBRATION_DEFAULT_STRENGTH);
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    return value;
}

void Config::set_engine_vibration_strength(int value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    cfg.put_int("controls.analog.haptic.engine_vibration_strength", value);
}

int Config::engine_period_ms()
{
    int value = cfg.get_int(
        "controls.analog.haptic.engine_period_ms",
        ENGINE_PERIOD_DEFAULT_MS);
    if (value < ENGINE_PERIOD_MIN_MS) value = ENGINE_PERIOD_MIN_MS;
    if (value > ENGINE_PERIOD_MAX_MS) value = ENGINE_PERIOD_MAX_MS;
    return value;
}

void Config::set_engine_period_ms(int value)
{
    const int current = engine_period_ms();
    /* The existing FFB menu supplies +/-5 for this physical timing value.
       Promote those menu nudges to the requested 10 ms steps while still
       allowing exact values loaded or written directly through config. */
    if (value == current - 5) value = current - 10;
    else if (value == current + 5) value = current + 10;
    if (value < ENGINE_PERIOD_MIN_MS) value = ENGINE_PERIOD_MIN_MS;
    if (value > ENGINE_PERIOD_MAX_MS) value = ENGINE_PERIOD_MAX_MS;
    cfg.put_int("controls.analog.haptic.engine_period_ms", value);
}

int Config::bumper_view_height_level()
{
    int level = cfg.get_int("engine.bumper_view_height", 1);
    if (level < 0 || level >= BUMPER_VIEW_HEIGHT_LEVELS)
        level = 1;
    return level;
}

void Config::set_bumper_view_height_level(int level)
{
    if (level < 0)
        level = 0;
    else if (level >= BUMPER_VIEW_HEIGHT_LEVELS)
        level = BUMPER_VIEW_HEIGHT_LEVELS - 1;
    cfg.put_int("engine.bumper_view_height", level);
}

void Config::cycle_bumper_view_height()
{
    set_bumper_view_height_level(
        (bumper_view_height_level() + 1) % BUMPER_VIEW_HEIGHT_LEVELS);
}

bool Config::ferrari_mirror_fix()
{
    return cfg.get_int("engine.ferrari_mirror_fix", 1) != 0;
}

void Config::set_ferrari_mirror_fix(bool enabled)
{
    cfg.put_int("engine.ferrari_mirror_fix", enabled ? 1 : 0);
}

void Config::toggle_ferrari_mirror_fix()
{
    set_ferrari_mirror_fix(!ferrari_mirror_fix());
}

bool Config::bugfix_read_setting(const char* name, bool fallback)
{
    const std::string path = std::string("engine.bugfixes.") + name;
    return cfg.get_int(path, fallback ? 1 : 0) != 0;
}

void Config::bugfix_write_setting(const char* name, bool enabled)
{
    cfg.put_int(std::string("engine.bugfixes.") + name, enabled ? 1 : 0);
}

bool Config::bugfix_steering_input()
{
    return bugfix_read_setting("steering_input", true);
}

bool Config::bugfix_checkpoint_lap_time()
{
    return bugfix_read_setting("checkpoint_lap_time", true);
}

bool Config::bugfix_ending_palette()
{
    return bugfix_read_setting("ending_palette", true);
}

bool Config::bugfix_music_select_tile()
{
    return bugfix_read_setting("music_select_tile", true);
}

bool Config::bugfix_menu_map_road_line()
{
    return bugfix_read_setting("menu_map_road_line", true);
}

bool Config::bugfix_crash_engine_sound()
{
    return bugfix_read_setting("crash_engine_sound", true);
}

bool Config::bugfix_wheel_slip_se()
{
    /* false keeps the original arcade/MAME slip detection by default. */
    return bugfix_read_setting("wheel_slip_se", false);
}

void Config::set_bugfix_steering_input(bool enabled)
{
    bugfix_write_setting("steering_input", enabled);
}

void Config::set_bugfix_checkpoint_lap_time(bool enabled)
{
    bugfix_write_setting("checkpoint_lap_time", enabled);
}

void Config::set_bugfix_ending_palette(bool enabled)
{
    bugfix_write_setting("ending_palette", enabled);
}

void Config::set_bugfix_music_select_tile(bool enabled)
{
    bugfix_write_setting("music_select_tile", enabled);
}

void Config::set_bugfix_menu_map_road_line(bool enabled)
{
    bugfix_write_setting("menu_map_road_line", enabled);
}

void Config::set_bugfix_crash_engine_sound(bool enabled)
{
    bugfix_write_setting("crash_engine_sound", enabled);
}

void Config::set_bugfix_wheel_slip_se(bool enabled)
{
    bugfix_write_setting("wheel_slip_se", enabled);
    /* The preserved Ferrari code still branches on this legacy runtime bit. */
    engine.fix_bugs = enabled;
}

void Config::sync_bugfix_runtime()
{
    /* Only Ferrari slip detection still consumes the legacy aggregate flag. */
    engine.fix_bugs = bugfix_wheel_slip_se();
}

void Config::reset_bugfix_settings()
{
    set_bugfix_steering_input(true);
    set_bugfix_checkpoint_lap_time(true);
    set_bugfix_ending_palette(true);
    set_bugfix_music_select_tile(true);
    set_bugfix_menu_map_road_line(true);
    set_bugfix_crash_engine_sound(true);
    set_bugfix_wheel_slip_se(false);
}

int Config::endless_read_setting(const char* name, int fallback, int minimum, int maximum)
{
    const std::string path = std::string("endless.") + name;
    int value = cfg.get_int(path, fallback);
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return value;
}

void Config::endless_write_setting(const char* name, int value, int minimum, int maximum)
{
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    cfg.put_int(std::string("endless.") + name, value);
}

bool Config::endless_random_start()
{
    return endless_read_setting("random_start", ENDLESS_DEFAULT_RANDOM_START, 0, 1) != 0;
}

void Config::set_endless_random_start(bool enabled)
{
    endless_write_setting("random_start", enabled ? 1 : 0, 0, 1);
}

int Config::endless_start_time()
{
    return endless_read_setting("start_time", ENDLESS_DEFAULT_START_TIME, 30, 99);
}

int Config::endless_checkpoint_time()
{
    return endless_read_setting("checkpoint_time", ENDLESS_DEFAULT_CHECKPOINT_TIME, 20, 99);
}

int Config::endless_time_decrease()
{
    return endless_read_setting("time_decrease", ENDLESS_DEFAULT_TIME_DECREASE, 0, 10);
}

int Config::endless_time_interval()
{
    return endless_read_setting("time_interval", ENDLESS_DEFAULT_TIME_INTERVAL, 1, 10);
}

int Config::endless_min_checkpoint()
{
    int value = endless_read_setting("min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT, 10, 99);
    const int checkpoint = endless_checkpoint_time();
    if (value > checkpoint) value = checkpoint;
    return value;
}

int Config::endless_start_traffic()
{
    return endless_read_setting("start_traffic", ENDLESS_DEFAULT_START_TRAFFIC, 0, 8);
}

int Config::endless_traffic_increase()
{
    return endless_read_setting("traffic_increase", ENDLESS_DEFAULT_TRAFFIC_INCREASE, 0, 8);
}

int Config::endless_traffic_interval()
{
    return endless_read_setting("traffic_interval", ENDLESS_DEFAULT_TRAFFIC_INTERVAL, 1, 10);
}

int Config::endless_max_traffic()
{
    int value = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8);
    const int start = endless_start_traffic();
    if (value < start) value = start;
    return value;
}

void Config::set_endless_start_time(int value)
{
    endless_write_setting("start_time", value, 30, 99);
}

void Config::set_endless_checkpoint_time(int value)
{
    endless_write_setting("checkpoint_time", value, 20, 99);
    const int checkpoint = endless_checkpoint_time();
    const int stored_min = endless_read_setting("min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT, 10, 99);
    if (stored_min > checkpoint)
        endless_write_setting("min_checkpoint", checkpoint, 10, 99);
}

void Config::set_endless_time_decrease(int value)
{
    endless_write_setting("time_decrease", value, 0, 10);
}

void Config::set_endless_time_interval(int value)
{
    endless_write_setting("time_interval", value, 1, 10);
}

void Config::set_endless_min_checkpoint(int value)
{
    if (value > endless_checkpoint_time()) value = endless_checkpoint_time();
    endless_write_setting("min_checkpoint", value, 10, 99);
}

void Config::set_endless_start_traffic(int value)
{
    endless_write_setting("start_traffic", value, 0, 8);
    const int start = endless_start_traffic();
    const int stored_max = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8);
    if (stored_max < start)
        endless_write_setting("max_traffic", start, 0, 8);
}

void Config::set_endless_traffic_increase(int value)
{
    endless_write_setting("traffic_increase", value, 0, 8);
}

void Config::set_endless_traffic_interval(int value)
{
    endless_write_setting("traffic_interval", value, 1, 10);
}

void Config::set_endless_max_traffic(int value)
{
    endless_write_setting("max_traffic", value, 0, 8);
    const int maximum = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8);
    if (endless_start_traffic() > maximum)
        endless_write_setting("start_traffic", maximum, 0, 8);
}

int Config::endless_checkpoint_for_stage(uint16_t stage)
{
    const int steps = static_cast<int>(stage) / endless_time_interval();
    int value = endless_checkpoint_time() - (steps * endless_time_decrease());
    const int minimum = endless_min_checkpoint();
    if (value < minimum) value = minimum;
    return value;
}

int Config::endless_traffic_for_stage(uint16_t stage)
{
    const int steps = static_cast<int>(stage) / endless_traffic_interval();
    int value = endless_start_traffic() + (steps * endless_traffic_increase());
    const int maximum = endless_max_traffic();
    if (value > maximum) value = maximum;
    return value;
}

uint8_t Config::endless_start_time_bcd()
{
    const int seconds = endless_start_time();
    return static_cast<uint8_t>(((seconds / 10) << 4) | (seconds % 10));
}

void Config::reset_endless_settings()
{
    cfg.put_int("endless.random_start", ENDLESS_DEFAULT_RANDOM_START);
    cfg.put_int("endless.start_time", ENDLESS_DEFAULT_START_TIME);
    cfg.put_int("endless.checkpoint_time", ENDLESS_DEFAULT_CHECKPOINT_TIME);
    cfg.put_int("endless.time_decrease", ENDLESS_DEFAULT_TIME_DECREASE);
    cfg.put_int("endless.time_interval", ENDLESS_DEFAULT_TIME_INTERVAL);
    cfg.put_int("endless.min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT);
    cfg.put_int("endless.start_traffic", ENDLESS_DEFAULT_START_TRAFFIC);
    cfg.put_int("endless.traffic_increase", ENDLESS_DEFAULT_TRAFFIC_INCREASE);
    cfg.put_int("endless.traffic_interval", ENDLESS_DEFAULT_TRAFFIC_INTERVAL);
    cfg.put_int("endless.max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC);
}
