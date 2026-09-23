from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# -----------------------------------------------------------------------------
# AUDIO: persist output device by name and allow live selection from SOUND menu.
# Adapted from CannonBall-SE 1.50 without importing its Pi-specific callback/RT
# scheduling work.
# -----------------------------------------------------------------------------
replace_once(
    "src/main/frontend/config_base.hpp",
    "    int playback_device; // omit from config file or set to -1 to use system default\n    int wave_volume;",
    "    int playback_device; // omit from config file or set to -1 to use system default\n    std::string playback_device_name; // stable SDL device name; preferred over the numeric index\n    int wave_volume;",
)

replace_once(
    "src/main/frontend/config_base.cpp",
    "    // Index of SDL playback device to request, -1 for default\n    sound.playback_device = cfg.get_int(\"sound.playback_device\", -1);\n",
    "    // Index of SDL playback device to request, -1 for default\n    sound.playback_device = cfg.get_int(\"sound.playback_device\", -1);\n    // Stable device name takes priority over the numeric index when available.\n    sound.playback_device_name = cfg.get_string(\"sound.playback_device_name\", \"\");\n",
)

replace_once(
    "src/main/frontend/config_base.cpp",
    "    cfg.put_int(\"sound.playback_device\",    sound.playback_device);  // JJP - Index of SDL playback device to request, -1 for default  \n    cfg.put_int(\"sound.wave_volume\",        sound.wave_volume);",
    "    cfg.put_int(\"sound.playback_device\",    sound.playback_device);  // JJP - Index fallback, -1 for default\n    cfg.put_string(\"sound.playback_device_name\", sound.playback_device_name); // Stable SDL device name\n    cfg.put_int(\"sound.wave_volume\",        sound.wave_volume);",
)

replace_once(
    "src/main/sdl2/audio.hpp",
    "#include <array>\n#include <string>\n#include <SDL.h>",
    "#include <array>\n#include <string>\n#include <vector>\n#include <SDL.h>",
)

replace_once(
    "src/main/sdl2/audio.hpp",
    "    void fill_and_mix(uint8_t *stream, int len);\n\nprivate:",
    "    void fill_and_mix(uint8_t *stream, int len);\n\n    // Stable playback-device discovery/selection, shared with the SOUND menu.\n    void refresh_playback_devices();\n    const std::vector<std::string>& playback_devices() const { return playback_devices_; }\n    const std::string& active_device_name() const { return active_device_name_; }\n    bool device_open() const { return dev != 0; }\n\nprivate:",
)

replace_once(
    "src/main/sdl2/audio.hpp",
    "    int audio_paused;\n    SDL_AudioDeviceID dev;\n\n    void clear_buffers();",
    "    int audio_paused;\n    SDL_AudioDeviceID dev;\n\n    std::vector<std::string> playback_devices_;\n    std::string active_device_name_;\n    enum { RULE_DEFAULT = 0, RULE_BY_NAME, RULE_BY_INDEX };\n    int active_device_rule_ = RULE_DEFAULT;\n\n    void clear_buffers();",
)

# Insert the enumeration helper immediately before start_audio().
replace_once(
    "src/main/sdl2/audio.cpp",
    "\n\nvoid Audio::start_audio(bool list_devices_only)\n{",
    '''\n\nvoid Audio::refresh_playback_devices()\n{\n    playback_devices_.clear();\n\n    // The SOUND menu can be opened while sound is disabled, so ensure SDL's\n    // audio subsystem exists before asking it for device names. start_audio()\n    // performs the normal platform-specific initialization afterwards.\n    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0)\n        SDL_InitSubSystem(SDL_INIT_AUDIO);\n\n    int count = SDL_GetNumAudioDevices(0);\n    if (count < 0) count = 0;\n    if (count > 32) count = 32;\n\n    for (int i = 0; i < count; ++i)\n    {\n        const char* name = SDL_GetAudioDeviceName(i, 0);\n        playback_devices_.emplace_back(name ? name : \"\");\n    }\n}\n\nvoid Audio::start_audio(bool list_devices_only)\n{''',
)

old_devices = '''        // Display available devices, user may wish to use a particular device e.g. external DAC\n        printf("Available audio devices:\\n");\n        int numDevices = SDL_GetNumAudioDevices(0); // 0 requests playback devices\n        if (numDevices > 32) {\n            // clamp to 32 max, probably way more than any setup will have\n            numDevices = 32;\n        }\n        const char* device_name[32];\n        memset(device_name, 0, sizeof(device_name));\n\n        for (int i = 0; i < numDevices; i++) {\n            device_name[i] = SDL_GetAudioDeviceName(i, 0);\n            printf("   %d: %s\\n", i, device_name[i]);\n        }\n\n        if (list_devices_only)\n            // request was to list the available SDL devices only, this is used in the main program\n            // with command-line option -list-sound-devices to help the user chose the sound device\n            // during the build process.\n            return;\n'''
new_devices = '''        // Keep a stable process-local list for startup resolution and the SOUND menu.\n        refresh_playback_devices();\n        const int numDevices = static_cast<int>(playback_devices_.size());\n\n        if (list_devices_only)\n        {\n            printf("Available audio devices:\\n");\n            for (int i = 0; i < numDevices; ++i)\n                printf("   %d: %s\\n", i, playback_devices_[i].c_str());\n            return;\n        }\n'''
replace_once("src/main/sdl2/audio.cpp", old_devices, new_devices)

old_pick = '''        const char* playback_device = NULL;\n        if (config.sound.playback_device != -1 && config.sound.playback_device < numDevices) {\n            // User has configured a particular output device; find its name\n            playback_device = device_name[config.sound.playback_device];\n        }\n\n        // SDL2 block\n'''
new_pick = '''        // Prefer the stable device name. If it disappeared, retain the old\n        // numeric index as a compatibility fallback, then finally use SDL's\n        // system default. Exact name match wins; substring match helps with\n        // backends that decorate an otherwise stable device name.\n        const char* playback_device = NULL;\n        active_device_name_.clear();\n        active_device_rule_ = RULE_DEFAULT;\n\n        if (!config.sound.playback_device_name.empty())\n        {\n            int match = -1;\n            for (int i = 0; i < numDevices; ++i)\n            {\n                if (playback_devices_[i] == config.sound.playback_device_name)\n                {\n                    match = i;\n                    break;\n                }\n            }\n            if (match < 0)\n            {\n                for (int i = 0; i < numDevices; ++i)\n                {\n                    if (playback_devices_[i].find(config.sound.playback_device_name) != std::string::npos)\n                    {\n                        match = i;\n                        break;\n                    }\n                }\n            }\n\n            if (match >= 0)\n            {\n                active_device_name_ = playback_devices_[match];\n                active_device_rule_ = RULE_BY_NAME;\n            }\n            else if (config.sound.playback_device >= 0 && config.sound.playback_device < numDevices)\n            {\n                std::cerr << "Warning: audio device \\\"" << config.sound.playback_device_name\n                          << "\\\" not found; using saved index " << config.sound.playback_device\n                          << " instead." << std::endl;\n                active_device_name_ = playback_devices_[config.sound.playback_device];\n                active_device_rule_ = RULE_BY_INDEX;\n            }\n            else\n            {\n                std::cerr << "Warning: audio device \\\"" << config.sound.playback_device_name\n                          << "\\\" not found; using system default." << std::endl;\n            }\n        }\n        else if (config.sound.playback_device >= 0)\n        {\n            if (config.sound.playback_device < numDevices)\n            {\n                active_device_name_ = playback_devices_[config.sound.playback_device];\n                active_device_rule_ = RULE_BY_INDEX;\n            }\n            else\n            {\n                std::cerr << "Warning: configured audio device index "\n                          << config.sound.playback_device << " is out of range; using system default."\n                          << std::endl;\n            }\n        }\n\n        if (active_device_rule_ != RULE_DEFAULT)\n            playback_device = active_device_name_.c_str();\n\n        // SDL2 block\n'''
replace_once("src/main/sdl2/audio.cpp", old_pick, new_pick)

replace_once(
    "src/main/sdl2/audio.cpp",
    "    SDL_CloseAudioDevice(dev);\n    sound_enabled = false;",
    "    SDL_CloseAudioDevice(dev);\n    dev = 0;\n    active_device_name_.clear();\n    active_device_rule_ = RULE_DEFAULT;\n    sound_enabled = false;",
)

# Menu label and font-safe display helper.
replace_once(
    "src/main/frontend/menulabels.hpp",
    "const static char* ENTRY_MUTE               = \"SOUND \";\n",
    "const static char* ENTRY_MUTE               = \"SOUND \";\nconst static char* ENTRY_OUTPUT_DEVICE      = \"OUTPUT DEVICE \";\n",
)

replace_once(
    "src/main/frontend/menu_base.cpp",
    "#define SELECTED(X) detail_menu_predicates::istarts_with(OPTION, X)\n\n\n\n// Logo Y Position",
    '''#define SELECTED(X) detail_menu_predicates::istarts_with(OPTION, X)\n\n// The System 16 font supports a deliberately small character set. Sanitise\n// external SDL device names before displaying them in menu rows.\nstatic std::string font_safe(const std::string& source)\n{\n    std::string out;\n    out.reserve(source.size());\n    bool previous_space = false;\n    for (unsigned char c : source)\n    {\n        char u = static_cast<char>(std::toupper(c));\n        const bool supported =\n            (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||\n            u == '-' || u == '.' || u == '%' || u == '!' || u == ',' || u == '/';\n\n        if (supported)\n        {\n            out += u;\n            previous_space = false;\n        }\n        else if (u == ' ' && !out.empty() && !previous_space)\n        {\n            out += ' ';\n            previous_space = true;\n        }\n    }\n    while (!out.empty() && out.back() == ' ')\n        out.pop_back();\n    return out;\n}\n\n// Logo Y Position''',
)

replace_once(
    "src/main/frontend/menu_base.cpp",
    "    menu_sound.push_back(ENTRY_MUTE);\n    menu_sound.push_back(ENTRY_ADVERTISE);",
    "    menu_sound.push_back(ENTRY_MUTE);\n    menu_sound.push_back(ENTRY_OUTPUT_DEVICE);\n    menu_sound.push_back(ENTRY_ADVERTISE);",
)

replace_once(
    "src/main/frontend/menu_base.cpp",
    "            else if (SELECTED(ENTRY_ADVERTISE))\n                config.sound.advertise ^= 1;",
    '''            else if (SELECTED(ENTRY_OUTPUT_DEVICE))\n            {\n                cannonball::audio.refresh_playback_devices();\n                const auto& devices = cannonball::audio.playback_devices();\n\n                int current = 0; // 0 = DEFAULT\n                const std::string wanted = !config.sound.playback_device_name.empty()\n                    ? config.sound.playback_device_name\n                    : cannonball::audio.active_device_name();\n\n                if (!wanted.empty())\n                {\n                    for (size_t i = 0; i < devices.size(); ++i)\n                    {\n                        if (devices[i] == wanted)\n                        {\n                            current = static_cast<int>(i) + 1;\n                            break;\n                        }\n                    }\n                }\n\n                int next = current + 1;\n                if (next > static_cast<int>(devices.size()))\n                    next = 0;\n\n                if (next == 0)\n                {\n                    config.sound.playback_device = -1;\n                    config.sound.playback_device_name.clear();\n                }\n                else\n                {\n                    config.sound.playback_device = next - 1;\n                    config.sound.playback_device_name = devices[next - 1];\n                }\n\n                if (config.sound.enabled)\n                {\n                    cannonball::audio.stop_audio();\n                    cannonball::audio.start_audio();\n                    if (!cannonball::audio.device_open())\n                        display_message("AUDIO DEVICE FAILED TO OPEN");\n                }\n            }\n            else if (SELECTED(ENTRY_ADVERTISE))\n                config.sound.advertise ^= 1;''',
)

replace_once(
    "src/main/frontend/menu_base.cpp",
    "            if (SELECTED(ENTRY_MUTE))               set_menu_text(ENTRY_MUTE, config.sound.enabled ? \"ON\" : \"OFF\");\n            else if (SELECTED(ENTRY_ADVERTISE))",
    '''            if (SELECTED(ENTRY_MUTE))               set_menu_text(ENTRY_MUTE, config.sound.enabled ? "ON" : "OFF");\n            else if (SELECTED(ENTRY_OUTPUT_DEVICE))\n            {\n                std::string text = config.sound.playback_device_name.empty()\n                    ? "DEFAULT" : font_safe(config.sound.playback_device_name);\n                const size_t label_len = std::string(ENTRY_OUTPUT_DEVICE).size();\n                const size_t max_len = label_len < 40 ? 40 - label_len : 0;\n                if (text.size() > max_len)\n                    text.resize(max_len);\n                set_menu_text(ENTRY_OUTPUT_DEVICE, text);\n            }\n            else if (SELECTED(ENTRY_ADVERTISE))''',
)


# -----------------------------------------------------------------------------
# ROM LOADER: retain DX's CRC/ZIP design, add SE 1.50's robust discovery rules.
# -----------------------------------------------------------------------------
replace_once(
    "src/main/romloader.cpp",
    "#include <cctype>\n\n#include <miniz.h>",
    "#include <cctype>\n\n#include <SDL.h>\n#include <miniz.h>",
)

marker = '''struct RomKey\n{\n'''
helpers = r'''// Loose ROM candidates larger than this are never read into memory. No
// supported OutRun chip is remotely this large; the generous cap simply
// prevents unrelated files in the ROM directory from being slurped in.
constexpr uintmax_t kMaxLooseFileSize = 0x100000;

static const char* const kKnownZipNames[] = {
    "outrun.zip", "outrunra.zip", "outrundx.zip", "outrundxa.zip",
    "outrundxj.zip", "outruneh.zip", "outruneha.zip", "outrundxeh.zip",
    "outrundxeha.zip", "outrunb.zip"
};

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool is_known_zip_name(const std::filesystem::path& path)
{
    const std::string filename = lower_copy(path.filename().string());
    for (const char* known : kKnownZipNames)
        if (filename == known)
            return true;
    return false;
}

std::vector<std::filesystem::path> list_regular_files_sorted(const std::filesystem::path& dir)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    const fs::directory_iterator end;
    while (!ec && it != end)
    {
        std::error_code entry_ec;
        if (it->is_regular_file(entry_ec) && !entry_ec)
            files.push_back(it->path());
        it.increment(ec);
    }
    std::sort(files.begin(), files.end(),
        [](const fs::path& a, const fs::path& b) {
            return a.filename().string() < b.filename().string();
        });
    return files;
}

struct RomKey
{
'''
replace_once("src/main/romloader.cpp", marker, helpers)

# Wrong loose ROMs should not hide a correct archive entry.
start = '''    // Preserve the original filename-based loose-ROM behaviour when the file\n    // exists. If it does not, transparently fall back to the CRC index so ZIP\n    // archives also work when data.crc32 is disabled.\n    if (!fs::exists(path))\n        return load_crc32(filename, offset, length, expected_crc, interleave, verbose);\n\n    std::vector<uint8_t> buffer;\n    if (!read_exact_file(path, length, buffer))\n    {\n        if (verbose)\n            std::cout << "cannot read rom or unexpected size: " << path.string() << std::endl;\n        loaded = false;\n        return 1;\n    }\n\n    const uint32_t crc = crc32(buffer.data(), buffer.size());\n\n    if (static_cast<uint32_t>(expected_crc) != crc)\n    {\n        if (verbose)\n            std::cout << std::hex\n                      << filename << " has incorrect checksum.\\nExpected: "\n                      << static_cast<uint32_t>(expected_crc) << " Found: " << crc\n                      << std::dec << std::endl;\n        loaded = false;\n        return 1;\n    }\n\n    copy_interleaved(rom, buffer, offset, interleave);\n    loaded = true;\n    return 0;\n'''
repl = '''    std::error_code ec;\n    const bool exists = fs::exists(path, ec) && !ec;\n    if (exists)\n    {\n        std::vector<uint8_t> buffer;\n        if (read_exact_file(path, length, buffer))\n        {\n            const uint32_t crc = crc32(buffer.data(), buffer.size());\n            if (static_cast<uint32_t>(expected_crc) == crc)\n            {\n                copy_interleaved(rom, buffer, offset, interleave);\n                loaded = true;\n                return 0;\n            }\n\n            if (verbose)\n                std::cout << std::hex << filename\n                          << " has incorrect checksum. Expected: "\n                          << static_cast<uint32_t>(expected_crc) << " Found: " << crc\n                          << std::dec << ". Trying the ROM index instead." << std::endl;\n        }\n        else if (verbose)\n        {\n            std::cout << "cannot read rom or unexpected size: " << path.string()\n                      << ". Trying the ROM index instead." << std::endl;\n        }\n    }\n\n    // Missing/stale/wrong loose file: do not let it mask a correct ROM inside\n    // a MAME archive or another searched location.\n    return load_crc32(filename, offset, length, expected_crc, interleave, verbose);\n'''
replace_once("src/main/romloader.cpp", start, repl)

# Replace create_map with a deterministic, non-throwing version that also
# checks known MAME archive names beside the executable/current directory.
p = Path("src/main/romloader.cpp")
text = p.read_text(encoding="utf-8")
func_start = text.index("int RomLoader::create_map()\n{")
func_end = text.index("\nint RomLoader::load_crc32", func_start)
new_func = r'''int RomLoader::create_map()
{
    namespace fs = std::filesystem;

    rom_map.clear();
    mapped_rom_path = config.data.rom_path;
    map_created = true;

    std::error_code ec;
    const fs::path source_path(config.data.rom_path);

    // DX feature retained: data.rompath may point directly at a ZIP archive.
    if (fs::is_regular_file(source_path, ec) && !ec && is_zip_path(source_path))
    {
        add_zip_to_map(source_path);
        return rom_map.empty() ? 1 : 0;
    }

    ec.clear();
    const bool have_rom_dir = fs::is_directory(source_path, ec) && !ec;
    if (!have_rom_dir)
        std::cout << "Warning: Could not open ROM directory - " << config.data.rom_path << std::endl;

    std::vector<fs::path> scanned_dirs;

    if (have_rom_dir)
    {
        scanned_dirs.push_back(source_path);
        std::vector<fs::path> archives;

        // Loose files first so they keep DX's established tie priority.
        for (const fs::path& entry : list_regular_files_sorted(source_path))
        {
            if (is_zip_path(entry))
            {
                archives.push_back(entry);
                continue;
            }

            std::error_code size_ec;
            const uintmax_t size = fs::file_size(entry, size_ec);
            if (size_ec || size > kMaxLooseFileSize)
                continue;

            add_loose_file_to_map(entry);
        }

        for (const fs::path& archive : archives)
            add_zip_to_map(archive);
    }

    // Also accept known OutRun MAME archive names in the current working
    // directory and beside the executable. Do not scan arbitrary ZIPs there.
    std::vector<fs::path> extra_dirs;
    fs::path cwd = fs::current_path(ec);
    if (!ec)
        extra_dirs.push_back(cwd);

    char* base_path = SDL_GetBasePath();
    if (base_path)
    {
        extra_dirs.emplace_back(base_path);
        SDL_free(base_path);
    }

    for (const fs::path& dir : extra_dirs)
    {
        bool duplicate = false;
        for (const fs::path& previous : scanned_dirs)
        {
            std::error_code eq_ec;
            if (fs::equivalent(dir, previous, eq_ec) && !eq_ec)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        scanned_dirs.push_back(dir);
        for (const fs::path& entry : list_regular_files_sorted(dir))
            if (is_known_zip_name(entry))
                add_zip_to_map(entry);
    }

    if (rom_map.empty())
    {
        std::cout << "Warning: Could not create CRC32 ROM map. Did you copy the ROM files "
                  << "or an OutRun MAME ZIP into a searched location?" << std::endl;
        return 1;
    }

    return 0;
}
'''
text = text[:func_start] + new_func + text[func_end:]
p.write_text(text, encoding="utf-8")

print("SE 1.50 stage 2 audio + ROM selective ports applied successfully")
