/***************************************************************************
    Time Trial finish helpers.

    Provides a lightweight read-only check used by finish presentation code
    before the Results screen captures and persists the completed run.
***************************************************************************/

#pragma once

#include <cstdint>
#include <string>

#include "../utils.hpp"
#include "engine/ostats.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "frontend/xml_parser.h"

namespace time_trial_finish
{
    inline int track_from_level(uint8_t level)
    {
        static const uint8_t LEVELS[15] =
        {
            0x00,
            0x09, 0x08,
            0x12, 0x11, 0x10,
            0x1B, 0x1A, 0x19, 0x18,
            0x24, 0x23, 0x22, 0x21, 0x20
        };

        for (int i = 0; i < 15; ++i)
        {
            if (LEVELS[i] == level)
                return i;
        }

        return -1;
    }

    inline bool is_new_course_record()
    {
        if (outrun.cannonball_mode != Outrun::MODE_TTRIAL ||
            outrun.ttrial.laps != 3 ||
            outrun.ttrial.current_lap < outrun.ttrial.laps)
        {
            return false;
        }

        const int track = track_from_level(outrun.ttrial.level);
        if (track < 0)
            return false;

        uint32_t total = 0;
        for (int lap = 0; lap < 3; ++lap)
        {
            const int counter = ostats.stage_counters[lap];
            if (counter <= 0)
                return false;

            total += static_cast<uint16_t>(counter);
        }

        if (total > 0xFFFF)
            total = 0xFFFF;

        xml_parser::ptree data("timetrial_scores");
        const std::string filename = config.engine.jap ?
            config.data.file_ttrial_jap : config.data.file_ttrial;

        uint16_t record = 0;
        if (xml_parser::read_xml(filename, data))
        {
            const std::string base =
                "time_trial.track" + Utils::to_string(track) +
                (outrun.ttrial.traffic ? ".traffic_on" : ".traffic_off");

            record = static_cast<uint16_t>(
                data.get_int(base + ".entry0.total", 0));

            // Compatibility with the pre-DX single-record format. Those old
            // records always belong to the traditional Traffic ON class.
            if (!record && outrun.ttrial.traffic)
            {
                record = static_cast<uint16_t>(
                    data.get_int(
                        "time_trial.record" + Utils::to_string(track) + ".total",
                        0));
            }
        }

        // With no stored record, the first valid three-lap run is the record.
        return !record || static_cast<uint16_t>(total) < record;
    }
}
