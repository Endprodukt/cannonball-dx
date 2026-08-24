#pragma once

/***************************************************************************
    CannonBall DX Unified High-Score Storage

    Keep the existing score systems and their logical filenames intact while
    storing every high-score table inside one physical highscores.xml file.
    Legacy hiscores*.xml files are imported once and removed only after the
    combined file has been written successfully.

    Physical sections:
      original_world / original_japan
      continuous_world / continuous_japan
      time_trial_world / time_trial_japan
      endless_world / endless_japan
***************************************************************************/

// Rename the low-level XML functions while including the existing TinyXML2
// wrapper. Callers that include config.hpp will use the routed functions below;
// the raw functions remain available here for physical file I/O.
#define read_xml read_xml_raw
#define write_xml write_xml_raw
#include "xml_parser.h"
#undef write_xml
#undef read_xml

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace xml_parser
{
namespace highscore_storage_detail
{
    struct ScoreSlot
    {
        const char* logical_filename;
        const char* section;
    };

    // highscores.xml itself is the live logical path for Original World. The
    // remaining legacy-looking names are virtual selectors after migration.
    static constexpr std::array<ScoreSlot, 9> SCORE_SLOTS = {{
        { "highscores.xml",                 "original_world" },
        { "hiscores.xml",                   "original_world" },
        { "hiscores_jap.xml",               "original_japan" },
        { "hiscores_continuous.xml",        "continuous_world" },
        { "hiscores_continuous_jap.xml",    "continuous_japan" },
        { "hiscores_timetrial.xml",         "time_trial_world" },
        { "hiscores_timetrial_jap.xml",     "time_trial_japan" },
        { "hiscores_endless.xml",           "endless_world" },
        { "hiscores_endless_jap.xml",       "endless_japan" },
    }};

    static constexpr std::array<ScoreSlot, 8> LEGACY_FILES = {{
        { "hiscores.xml",                   "original_world" },
        { "hiscores_jap.xml",               "original_japan" },
        { "hiscores_continuous.xml",        "continuous_world" },
        { "hiscores_continuous_jap.xml",    "continuous_japan" },
        { "hiscores_timetrial.xml",         "time_trial_world" },
        { "hiscores_timetrial_jap.xml",     "time_trial_japan" },
        { "hiscores_endless.xml",           "endless_world" },
        { "hiscores_endless_jap.xml",       "endless_japan" },
    }};

    inline std::string basename(const std::string& path)
    {
        const std::size_t pos = path.find_last_of("/\\");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    inline std::string directory(const std::string& path)
    {
        const std::size_t pos = path.find_last_of("/\\");
        return pos == std::string::npos ? std::string() : path.substr(0, pos + 1);
    }

    inline const ScoreSlot* slot_for_path(const std::string& path)
    {
        const std::string name = basename(path);
        for (const ScoreSlot& slot : SCORE_SLOTS)
        {
            if (name == slot.logical_filename)
                return &slot;
        }
        return nullptr;
    }

    inline std::string unified_path_for(const std::string& logical_path)
    {
        return directory(logical_path) + "highscores.xml";
    }

    inline void reset_tree(ptree& tree, const char* root_name)
    {
        tree.doc.Clear();
        tree.root = tree.doc.NewElement(root_name);
        tree.doc.InsertFirstChild(tree.root);
    }

    inline bool is_unified_tree(const ptree& tree)
    {
        return tree.root && tree.root->Name() &&
               std::strcmp(tree.root->Name(), "highscores") == 0;
    }

    inline bool load_unified(const std::string& path, ptree& tree)
    {
        if (!read_xml_raw(path, tree, parse_mode_t::strict) ||
            !is_unified_tree(tree))
        {
            reset_tree(tree, "highscores");
            return false;
        }
        return true;
    }

    inline tinyxml2::XMLElement* section_root(
        ptree& tree,
        const char* section)
    {
        tinyxml2::XMLElement* container =
            tree.root ? tree.root->FirstChildElement(section) : nullptr;
        return container ? container->FirstChildElement() : nullptr;
    }

    inline void replace_section(
        ptree& combined,
        const char* section_name,
        const tinyxml2::XMLElement* source_root)
    {
        if (!combined.root || !source_root)
            return;

        if (tinyxml2::XMLElement* old =
                combined.root->FirstChildElement(section_name))
        {
            combined.root->DeleteChild(old);
        }

        tinyxml2::XMLElement* section =
            combined.doc.NewElement(section_name);
        section->InsertEndChild(source_root->DeepClone(&combined.doc));
        combined.root->InsertEndChild(section);
    }

    inline bool migrate_legacy_files(const std::string& unified_path)
    {
        ptree combined("highscores");
        bool unified_valid = load_unified(unified_path, combined);
        bool changed = false;
        std::vector<std::string> cleanup;
        const std::string dir = directory(unified_path);

        for (const ScoreSlot& legacy_slot : LEGACY_FILES)
        {
            const std::string legacy_path =
                dir + legacy_slot.logical_filename;

            ptree legacy("scores");
            if (!read_xml_raw(legacy_path, legacy))
                continue;

            // An existing unified section wins. This makes migration idempotent
            // and prevents a stale legacy file from overwriting newer DX data.
            if (!section_root(combined, legacy_slot.section) && legacy.root)
            {
                replace_section(combined, legacy_slot.section, legacy.root);
                changed = true;
            }

            cleanup.push_back(legacy_path);
        }

        if (changed)
        {
            if (!write_xml_raw(unified_path, combined))
                return unified_valid;
            unified_valid = true;
        }

        // Never delete source files unless a valid combined file exists.
        if (unified_valid)
        {
            for (const std::string& path : cleanup)
                std::remove(path.c_str());
        }

        return unified_valid;
    }

    inline bool copy_section_to_tree(
        ptree& combined,
        const char* section,
        ptree& destination)
    {
        tinyxml2::XMLElement* source = section_root(combined, section);
        if (!source)
            return false;

        destination.doc.Clear();
        tinyxml2::XMLNode* clone = source->DeepClone(&destination.doc);
        if (!clone)
            return false;

        destination.doc.InsertFirstChild(clone);
        destination.root = clone->ToElement();
        return destination.root != nullptr;
    }
}

// Public one-shot migration hook used during Config::load(). It also makes the
// old files disappear immediately rather than waiting for a specific mode to
// touch its score table.
inline bool migrate_highscores(const std::string& save_path)
{
    return highscore_storage_detail::migrate_legacy_files(
        save_path + "highscores.xml");
}

inline bool read_xml(const std::string& filename,
                     ptree& tree,
                     parse_mode_t mode = parse_mode)
{
    using namespace highscore_storage_detail;

    const ScoreSlot* slot = slot_for_path(filename);
    if (!slot)
        return read_xml_raw(filename, tree, mode);

    const std::string unified_path = unified_path_for(filename);
    migrate_legacy_files(unified_path);

    ptree combined("highscores");
    if (!load_unified(unified_path, combined))
        return false;

    return copy_section_to_tree(combined, slot->section, tree);
}

inline bool write_xml(
    const std::string& filename,
    ptree& tree,
    const std::string& xml_declaration =
        R"(<?xml version="1.0" encoding="UTF-8"?>)")
{
    using namespace highscore_storage_detail;

    const ScoreSlot* slot = slot_for_path(filename);
    if (!slot)
        return write_xml_raw(filename, tree, xml_declaration);

    if (!tree.root)
        return false;

    const std::string unified_path = unified_path_for(filename);
    migrate_legacy_files(unified_path);

    ptree combined("highscores");
    load_unified(unified_path, combined);
    replace_section(combined, slot->section, tree.root);

    if (!write_xml_raw(unified_path, combined, xml_declaration))
        return false;

    // A logical legacy filename is only a selector now. Remove a stale physical
    // copy if one still exists after a successful combined write.
    if (basename(filename) != "highscores.xml")
        std::remove(filename.c_str());

    return true;
}

} // namespace xml_parser
