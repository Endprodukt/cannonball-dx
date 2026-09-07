/***************************************************************************
    XML Configuration File Handling - CannonBall DX extensions.

    The current configuration declaration is preserved in config_base.hpp.
    This wrapper adds DX-only persistent settings without duplicating the
    inherited configuration structure.
***************************************************************************/

#pragma once

// Pre-include config_base.hpp dependencies before temporarily extending the
// class at its private section. This keeps the macro away from library headers.
#include <SDL.h>
#include <set>
#include <string>
#include <vector>
#include "stdint.hpp"
#include "highscore_storage.hpp"

#define CANNONBALL_DX_CONFIG_EXTENSIONS \
    static constexpr int BUMPER_VIEW_HEIGHT_LEVELS = 5; \
    static constexpr int ENGINE_VIBRATION_DEFAULT_STRENGTH = 7; \
    static constexpr int ENGINE_PERIOD_DEFAULT_MS = 130; \
    static constexpr int ENGINE_PERIOD_MIN_MS = 20; \
    static constexpr int ENGINE_PERIOD_MAX_MS = 250; \
    static constexpr int ENDLESS_DEFAULT_START_TIME = 80; \
    static constexpr int ENDLESS_DEFAULT_CHECKPOINT_TIME = 55; \
    static constexpr int ENDLESS_DEFAULT_TIME_DECREASE = 2; \
    static constexpr int ENDLESS_DEFAULT_TIME_INTERVAL = 3; \
    static constexpr int ENDLESS_DEFAULT_MIN_CHECKPOINT = 30; \
    static constexpr int ENDLESS_DEFAULT_START_TRAFFIC = 2; \
    static constexpr int ENDLESS_DEFAULT_TRAFFIC_INCREASE = 1; \
    static constexpr int ENDLESS_DEFAULT_TRAFFIC_INTERVAL = 3; \
    static constexpr int ENDLESS_DEFAULT_MAX_TRAFFIC = 8; \
    static constexpr int ENDLESS_DEFAULT_RANDOM_START = 0; \
    int radio_key() \
    { \
        return cfg.get_int("controls.radio.keyboard", -1); \
    } \
    void set_radio_key(int key) \
    { \
        cfg.put_int("controls.radio.keyboard", key); \
    } \
    int radio_binding_type(int group) \
    { \
        return cfg.get_int( \
            group == 0 ? "controls.radio.gamepad.type" : "controls.radio.wheel.type", \
            -1); \
    } \
    int radio_binding_index(int group) \
    { \
        return cfg.get_int( \
            group == 0 ? "controls.radio.gamepad.index" : "controls.radio.wheel.index", \
            -1); \
    } \
    int radio_binding_value(int group) \
    { \
        return cfg.get_int( \
            group == 0 ? "controls.radio.gamepad.value" : "controls.radio.wheel.value", \
            0); \
    } \
    std::string radio_binding_device(int group) \
    { \
        return cfg.get_string( \
            group == 0 ? "controls.radio.gamepad.device" : "controls.radio.wheel.device", \
            ""); \
    } \
    void set_radio_binding(int group, int type, int index, int value, const std::string& device) \
    { \
        const char* type_path = group == 0 ? "controls.radio.gamepad.type" : "controls.radio.wheel.type"; \
        const char* index_path = group == 0 ? "controls.radio.gamepad.index" : "controls.radio.wheel.index"; \
        const char* value_path = group == 0 ? "controls.radio.gamepad.value" : "controls.radio.wheel.value"; \
        const char* device_path = group == 0 ? "controls.radio.gamepad.device" : "controls.radio.wheel.device"; \
        cfg.put_int(type_path, type); \
        cfg.put_int(index_path, index); \
        cfg.put_int(value_path, value); \
        cfg.put_string(device_path, device); \
    } \
    void clear_radio_binding(int group) \
    { \
        set_radio_binding(group, -1, -1, 0, ""); \
    } \
    int engine_vibration_strength() \
    { \
        int value = cfg.get_int( \
            "controls.analog.haptic.engine_vibration_strength", \
            ENGINE_VIBRATION_DEFAULT_STRENGTH); \
        if (value < 0) value = 0; \
        if (value > 100) value = 100; \
        return value; \
    } \
    void set_engine_vibration_strength(int value) \
    { \
        if (value < 0) value = 0; \
        if (value > 100) value = 100; \
        cfg.put_int("controls.analog.haptic.engine_vibration_strength", value); \
    } \
    int engine_period_ms() \
    { \
        int value = cfg.get_int( \
            "controls.analog.haptic.engine_period_ms", \
            ENGINE_PERIOD_DEFAULT_MS); \
        if (value < ENGINE_PERIOD_MIN_MS) value = ENGINE_PERIOD_MIN_MS; \
        if (value > ENGINE_PERIOD_MAX_MS) value = ENGINE_PERIOD_MAX_MS; \
        return value; \
    } \
    void set_engine_period_ms(int value) \
    { \
        if (value < ENGINE_PERIOD_MIN_MS) value = ENGINE_PERIOD_MIN_MS; \
        if (value > ENGINE_PERIOD_MAX_MS) value = ENGINE_PERIOD_MAX_MS; \
        cfg.put_int("controls.analog.haptic.engine_period_ms", value); \
    } \
    int bumper_view_height_level() \
    { \
        int level = cfg.get_int("engine.bumper_view_height", 1); \
        if (level < 0 || level >= BUMPER_VIEW_HEIGHT_LEVELS) \
            level = 1; \
        return level; \
    } \
    void set_bumper_view_height_level(int level) \
    { \
        if (level < 0) \
            level = 0; \
        else if (level >= BUMPER_VIEW_HEIGHT_LEVELS) \
            level = BUMPER_VIEW_HEIGHT_LEVELS - 1; \
        cfg.put_int("engine.bumper_view_height", level); \
    } \
    void cycle_bumper_view_height() \
    { \
        set_bumper_view_height_level( \
            (bumper_view_height_level() + 1) % BUMPER_VIEW_HEIGHT_LEVELS); \
    } \
    bool ferrari_mirror_fix() \
    { \
        return cfg.get_int("engine.ferrari_mirror_fix", 1) != 0; \
    } \
    void set_ferrari_mirror_fix(bool enabled) \
    { \
        cfg.put_int("engine.ferrari_mirror_fix", enabled ? 1 : 0); \
    } \
    void toggle_ferrari_mirror_fix() \
    { \
        set_ferrari_mirror_fix(!ferrari_mirror_fix()); \
    } \
    int endless_read_setting(const char* name, int fallback, int minimum, int maximum) \
    { \
        const std::string path = std::string("endless.") + name; \
        int value = cfg.get_int(path, fallback); \
        if (value < minimum) value = minimum; \
        if (value > maximum) value = maximum; \
        return value; \
    } \
    void endless_write_setting(const char* name, int value, int minimum, int maximum) \
    { \
        if (value < minimum) value = minimum; \
        if (value > maximum) value = maximum; \
        cfg.put_int(std::string("endless.") + name, value); \
    } \
    bool endless_random_start() \
    { \
        return endless_read_setting("random_start", ENDLESS_DEFAULT_RANDOM_START, 0, 1) != 0; \
    } \
    void set_endless_random_start(bool enabled) \
    { \
        endless_write_setting("random_start", enabled ? 1 : 0, 0, 1); \
    } \
    int endless_start_time() \
    { \
        return endless_read_setting("start_time", ENDLESS_DEFAULT_START_TIME, 30, 99); \
    } \
    int endless_checkpoint_time() \
    { \
        return endless_read_setting("checkpoint_time", ENDLESS_DEFAULT_CHECKPOINT_TIME, 20, 99); \
    } \
    int endless_time_decrease() \
    { \
        return endless_read_setting("time_decrease", ENDLESS_DEFAULT_TIME_DECREASE, 0, 10); \
    } \
    int endless_time_interval() \
    { \
        return endless_read_setting("time_interval", ENDLESS_DEFAULT_TIME_INTERVAL, 1, 10); \
    } \
    int endless_min_checkpoint() \
    { \
        int value = endless_read_setting("min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT, 10, 99); \
        const int checkpoint = endless_checkpoint_time(); \
        if (value > checkpoint) value = checkpoint; \
        return value; \
    } \
    int endless_start_traffic() \
    { \
        return endless_read_setting("start_traffic", ENDLESS_DEFAULT_START_TRAFFIC, 0, 8); \
    } \
    int endless_traffic_increase() \
    { \
        return endless_read_setting("traffic_increase", ENDLESS_DEFAULT_TRAFFIC_INCREASE, 0, 8); \
    } \
    int endless_traffic_interval() \
    { \
        return endless_read_setting("traffic_interval", ENDLESS_DEFAULT_TRAFFIC_INTERVAL, 1, 10); \
    } \
    int endless_max_traffic() \
    { \
        int value = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8); \
        const int start = endless_start_traffic(); \
        if (value < start) value = start; \
        return value; \
    } \
    void set_endless_start_time(int value) \
    { \
        endless_write_setting("start_time", value, 30, 99); \
    } \
    void set_endless_checkpoint_time(int value) \
    { \
        endless_write_setting("checkpoint_time", value, 20, 99); \
        const int checkpoint = endless_checkpoint_time(); \
        const int stored_min = endless_read_setting("min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT, 10, 99); \
        if (stored_min > checkpoint) \
            endless_write_setting("min_checkpoint", checkpoint, 10, 99); \
    } \
    void set_endless_time_decrease(int value) \
    { \
        endless_write_setting("time_decrease", value, 0, 10); \
    } \
    void set_endless_time_interval(int value) \
    { \
        endless_write_setting("time_interval", value, 1, 10); \
    } \
    void set_endless_min_checkpoint(int value) \
    { \
        if (value > endless_checkpoint_time()) value = endless_checkpoint_time(); \
        endless_write_setting("min_checkpoint", value, 10, 99); \
    } \
    void set_endless_start_traffic(int value) \
    { \
        endless_write_setting("start_traffic", value, 0, 8); \
        const int start = endless_start_traffic(); \
        const int stored_max = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8); \
        if (stored_max < start) \
            endless_write_setting("max_traffic", start, 0, 8); \
    } \
    void set_endless_traffic_increase(int value) \
    { \
        endless_write_setting("traffic_increase", value, 0, 8); \
    } \
    void set_endless_traffic_interval(int value) \
    { \
        endless_write_setting("traffic_interval", value, 1, 10); \
    } \
    void set_endless_max_traffic(int value) \
    { \
        endless_write_setting("max_traffic", value, 0, 8); \
        const int maximum = endless_read_setting("max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC, 0, 8); \
        if (endless_start_traffic() > maximum) \
            endless_write_setting("start_traffic", maximum, 0, 8); \
    } \
    int endless_checkpoint_for_stage(uint16_t stage) \
    { \
        const int steps = static_cast<int>(stage) / endless_time_interval(); \
        int value = endless_checkpoint_time() - (steps * endless_time_decrease()); \
        const int minimum = endless_min_checkpoint(); \
        if (value < minimum) value = minimum; \
        return value; \
    } \
    int endless_traffic_for_stage(uint16_t stage) \
    { \
        const int steps = static_cast<int>(stage) / endless_traffic_interval(); \
        int value = endless_start_traffic() + (steps * endless_traffic_increase()); \
        const int maximum = endless_max_traffic(); \
        if (value > maximum) value = maximum; \
        return value; \
    } \
    uint8_t endless_start_time_bcd() \
    { \
        const int seconds = endless_start_time(); \
        return static_cast<uint8_t>(((seconds / 10) << 4) | (seconds % 10)); \
    } \
    void reset_endless_settings() \
    { \
        cfg.put_int("endless.random_start", ENDLESS_DEFAULT_RANDOM_START); \
        cfg.put_int("endless.start_time", ENDLESS_DEFAULT_START_TIME); \
        cfg.put_int("endless.checkpoint_time", ENDLESS_DEFAULT_CHECKPOINT_TIME); \
        cfg.put_int("endless.time_decrease", ENDLESS_DEFAULT_TIME_DECREASE); \
        cfg.put_int("endless.time_interval", ENDLESS_DEFAULT_TIME_INTERVAL); \
        cfg.put_int("endless.min_checkpoint", ENDLESS_DEFAULT_MIN_CHECKPOINT); \
        cfg.put_int("endless.start_traffic", ENDLESS_DEFAULT_START_TRAFFIC); \
        cfg.put_int("endless.traffic_increase", ENDLESS_DEFAULT_TRAFFIC_INCREASE); \
        cfg.put_int("endless.traffic_interval", ENDLESS_DEFAULT_TRAFFIC_INTERVAL); \
        cfg.put_int("endless.max_traffic", ENDLESS_DEFAULT_MAX_TRAFFIC); \
    }

#define private public: CANNONBALL_DX_CONFIG_EXTENSIONS private
#include "config_base.hpp"
#undef private
#undef CANNONBALL_DX_CONFIG_EXTENSIONS
