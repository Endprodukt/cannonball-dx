#pragma once

#include <string>

#include "frontend/config.hpp"
#include "frontend/xml_parser.h"

// Optional external-output transport settings. SmartyPi remains independent.
struct ExternalOutputSettings
{
    bool loaded = false;

    bool network = true;
    bool windows = true;
    int port = 8000;

    void load_once()
    {
        if (loaded)
            return;

        loaded = true;

        xml_parser::ptree tree;
        bool found = xml_parser::read_xml(config.data.cfg_file, tree);

        if (!found)
        {
            const std::string fallback = config.data.res_path + config.data.cfg_file;
            found = xml_parser::read_xml(fallback, tree);
        }

        if (!found)
            return;

        network = tree.get_int("outputs.network", 1) != 0;
        windows = tree.get_int("outputs.windows", 1) != 0;
        port = tree.get_int("outputs.port", 8000);
    }
};

inline ExternalOutputSettings& external_output_settings()
{
    static ExternalOutputSettings settings;
    settings.load_once();
    return settings;
}
