/***************************************************************************
    Binary File Loader.

    Handles loading an individual binary file to memory.
    Supports reading bytes, words and longs from this area of memory.

    Copyright Chris White.
    See license.txt for more details.

    Refactored to remove Boost and Dirent dependency.
    Uses std::filesystem for directory scanning and a small CRC32 impl.

    CannonBall DX adds transparent ZIP archive support while retaining the
    existing loose-ROM behaviour. ROMs are indexed by CRC32 and uncompressed
    size, so current MAME merged/split archives and legacy extracted sets can
    coexist in the same ROM directory.
***************************************************************************/

#include <iostream>
#include <fstream>
#include <cstddef>
#include <unordered_map>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>

#include <miniz.h>

#include "stdint.hpp"
#include "romloader.hpp"
#include "frontend/config.hpp"

// -------------------------------
// Local CRC32 (IEEE 802.3) impl
// -------------------------------
namespace {

inline const uint32_t* crc32_table()
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    return table;
}

inline uint32_t crc32(const void* data, std::size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t c = 0xFFFFFFFFu;
    const uint32_t* T = crc32_table();
    for (std::size_t i = 0; i < n; ++i)
        c = T[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct RomKey
{
    uint32_t crc;
    uint32_t size;

    bool operator==(const RomKey& other) const
    {
        return crc == other.crc && size == other.size;
    }
};

struct RomKeyHash
{
    std::size_t operator()(const RomKey& key) const
    {
        return (static_cast<std::size_t>(key.crc) << 1) ^
               static_cast<std::size_t>(key.size);
    }
};

enum class RomSourceType
{
    LooseFile,
    ZipEntry
};

struct RomSource
{
    RomSourceType type = RomSourceType::LooseFile;
    std::string path;
    mz_uint file_index = 0;
    std::string entry_name;
};

static std::unordered_map<RomKey, RomSource, RomKeyHash> rom_map;
static bool map_created = false;
static std::string mapped_rom_path;

bool is_zip_path(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".zip";
}

bool read_entire_file(const std::filesystem::path& path, std::vector<uint8_t>& buffer)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const uintmax_t file_size = fs::file_size(path, ec);
    if (ec || file_size > static_cast<uintmax_t>(std::numeric_limits<uint32_t>::max()))
        return false;

    buffer.resize(static_cast<std::size_t>(file_size));

    std::ifstream src(path, std::ios::in | std::ios::binary);
    if (!src)
        return false;

    if (!buffer.empty())
        src.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));

    return src.good() || (src.eof() && static_cast<std::size_t>(src.gcount()) == buffer.size());
}

bool read_exact_file(const std::filesystem::path& path, const int expected_length, std::vector<uint8_t>& buffer)
{
    if (expected_length < 0)
        return false;

    std::error_code ec;
    const uintmax_t file_size = std::filesystem::file_size(path, ec);
    if (ec || file_size != static_cast<uintmax_t>(expected_length))
        return false;

    buffer.resize(static_cast<std::size_t>(expected_length));

    std::ifstream src(path, std::ios::in | std::ios::binary);
    if (!src)
        return false;

    if (!buffer.empty())
        src.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));

    return static_cast<std::size_t>(src.gcount()) == buffer.size();
}

void add_loose_file_to_map(const std::filesystem::path& path)
{
    std::vector<uint8_t> buffer;
    if (!read_entire_file(path, buffer))
        return;

    const RomKey key {
        crc32(buffer.data(), buffer.size()),
        static_cast<uint32_t>(buffer.size())
    };

    // Loose files deliberately win over archives when identical data exists in
    // both places. This preserves the behaviour expected by existing installs.
    rom_map.emplace(key, RomSource { RomSourceType::LooseFile, path.string(), 0, std::string() });
}

void add_zip_to_map(const std::filesystem::path& archive_path)
{
    mz_zip_archive zip {};
    if (!mz_zip_reader_init_file(&zip, archive_path.string().c_str(), 0))
    {
        std::cout << "Warning: Could not open ROM archive - " << archive_path.string() << std::endl;
        return;
    }

    const mz_uint file_count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < file_count; ++i)
    {
        mz_zip_archive_file_stat stat {};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        if (stat.m_is_directory || !stat.m_is_supported || stat.m_is_encrypted)
            continue;
        if (stat.m_uncomp_size > static_cast<mz_uint64>(std::numeric_limits<uint32_t>::max()))
            continue;

        const RomKey key {
            static_cast<uint32_t>(stat.m_crc32),
            static_cast<uint32_t>(stat.m_uncomp_size)
        };

        rom_map.emplace(key, RomSource {
            RomSourceType::ZipEntry,
            archive_path.string(),
            i,
            stat.m_filename
        });
    }

    mz_zip_reader_end(&zip);
}

bool load_source(const RomSource& source, const int expected_length, std::vector<uint8_t>& buffer, const bool verbose)
{
    if (source.type == RomSourceType::LooseFile)
    {
        if (!read_exact_file(source.path, expected_length, buffer))
        {
            if (verbose)
                std::cout << "cannot read rom: " << source.path << std::endl;
            return false;
        }
        return true;
    }

    mz_zip_archive zip {};
    if (!mz_zip_reader_init_file(&zip, source.path.c_str(), 0))
    {
        if (verbose)
            std::cout << "cannot open rom archive: " << source.path << std::endl;
        return false;
    }

    mz_zip_archive_file_stat stat {};
    if (!mz_zip_reader_file_stat(&zip, source.file_index, &stat) ||
        stat.m_is_directory || !stat.m_is_supported || stat.m_is_encrypted ||
        stat.m_uncomp_size != static_cast<mz_uint64>(expected_length))
    {
        if (verbose)
            std::cout << "invalid rom entry in archive: " << source.path
                      << " -> " << source.entry_name << std::endl;
        mz_zip_reader_end(&zip);
        return false;
    }

    buffer.resize(static_cast<std::size_t>(expected_length));
    const mz_bool extracted = mz_zip_reader_extract_to_mem(
        &zip,
        source.file_index,
        buffer.data(),
        buffer.size(),
        0);

    if (!extracted && verbose)
    {
        const mz_zip_error err = mz_zip_get_last_error(&zip);
        std::cout << "cannot extract rom from archive: " << source.path
                  << " -> " << source.entry_name
                  << " (" << mz_zip_get_error_string(err) << ")" << std::endl;
    }

    mz_zip_reader_end(&zip);
    return extracted == MZ_TRUE;
}

void copy_interleaved(uint8_t* destination, const std::vector<uint8_t>& buffer,
                      const int offset, const uint8_t interleave)
{
    for (std::size_t i = 0; i < buffer.size(); ++i)
        destination[(i * interleave) + static_cast<std::size_t>(offset)] = buffer[i];
}

} // namespace

RomLoader::RomLoader()
{
    rom = NULL;
    loaded = false;
}

RomLoader::~RomLoader()
{
    if (rom != NULL)
        delete[] rom;
}

void RomLoader::init(const uint32_t length)
{
    load = config.data.crc32 ? &RomLoader::load_crc32 : &RomLoader::load_rom;
    this->length = length;
    rom = new uint8_t[length];
}

void RomLoader::unload(void)
{
    delete[] rom;
    rom = NULL;
}

int RomLoader::load_rom(const char* filename, const int offset, const int length, const int expected_crc, const uint8_t interleave, const bool verbose)
{
    namespace fs = std::filesystem;

    const fs::path base(config.data.rom_path);
    const fs::path path = base / filename;

    // Preserve the original filename-based loose-ROM behaviour when the file
    // exists. If it does not, transparently fall back to the CRC index so ZIP
    // archives also work when data.crc32 is disabled.
    if (!fs::exists(path))
        return load_crc32(filename, offset, length, expected_crc, interleave, verbose);

    std::vector<uint8_t> buffer;
    if (!read_exact_file(path, length, buffer))
    {
        if (verbose)
            std::cout << "cannot read rom or unexpected size: " << path.string() << std::endl;
        loaded = false;
        return 1;
    }

    const uint32_t crc = crc32(buffer.data(), buffer.size());

    if (expected_crc != static_cast<int>(crc))
    {
        if (verbose)
            std::cout << std::hex
                      << filename << " has incorrect checksum.\nExpected: "
                      << expected_crc << " Found: " << crc << std::dec << std::endl;
        loaded = false;
        return 1;
    }

    copy_interleaved(rom, buffer, offset, interleave);
    loaded = true;
    return 0;
}

int RomLoader::create_map()
{
    namespace fs = std::filesystem;

    rom_map.clear();
    mapped_rom_path = config.data.rom_path;
    map_created = true;

    const fs::path source_path(config.data.rom_path);

    // Also accept data.rompath pointing directly at a ZIP file. The normal and
    // documented form remains a directory such as roms/.
    if (fs::exists(source_path) && fs::is_regular_file(source_path) && is_zip_path(source_path))
    {
        add_zip_to_map(source_path);
        return rom_map.empty() ? 1 : 0;
    }

    if (!fs::exists(source_path) || !fs::is_directory(source_path))
    {
        std::cout << "Warning: Could not open ROM directory - " << config.data.rom_path << std::endl;
        return 1;
    }

    std::vector<fs::path> archives;

    // First index extracted ROMs. They win ties over identical archive entries
    // for maximum backwards compatibility with existing CannonBall installs.
    for (const auto& entry : fs::directory_iterator(source_path))
    {
        if (!entry.is_regular_file())
            continue;

        if (is_zip_path(entry.path()))
            archives.push_back(entry.path());
        else
            add_loose_file_to_map(entry.path());
    }

    // Then index ZIP central directories. No ROM data is decompressed here;
    // miniz exposes CRC32 and uncompressed size directly from each entry.
    for (const fs::path& archive : archives)
        add_zip_to_map(archive);

    if (rom_map.empty())
    {
        std::cout << "Warning: Could not create CRC32 ROM map. "
                  << "Did you copy the ROM files or a MAME ZIP into the directory?" << std::endl;
        return 1;
    }

    return 0;
}

int RomLoader::load_crc32(const char* debug, const int offset, const int length, const int expected_crc, const uint8_t interleave, const bool verbose)
{
    if (!map_created || mapped_rom_path != config.data.rom_path)
        create_map();

    if (rom_map.empty())
        return 1;

    const RomKey key {
        static_cast<uint32_t>(expected_crc),
        static_cast<uint32_t>(length)
    };

    const auto search = rom_map.find(key);

    if (search == rom_map.end())
    {
        if (verbose)
            std::cout << "Unable to locate rom in path: " << config.data.rom_path
                      << " possible name: " << debug
                      << " crc32: 0x" << std::hex << static_cast<uint32_t>(expected_crc)
                      << " size: 0x" << length << std::dec << std::endl;
        loaded = false;
        return 1;
    }

    std::vector<uint8_t> buffer;
    if (!load_source(search->second, length, buffer, verbose))
    {
        loaded = false;
        return 1;
    }

    // Re-check the uncompressed bytes. ZIP central-directory CRCs are useful
    // for indexing, but this keeps the same data-integrity guarantee as loose
    // ROM loading and catches a modified/corrupt archive after indexing.
    const uint32_t crc = crc32(buffer.data(), buffer.size());
    if (crc != static_cast<uint32_t>(expected_crc))
    {
        if (verbose)
            std::cout << "ROM checksum changed while loading " << debug
                      << ". Expected: " << std::hex << static_cast<uint32_t>(expected_crc)
                      << " Found: " << crc << std::dec << std::endl;
        loaded = false;
        return 1;
    }

    copy_interleaved(rom, buffer, offset, interleave);
    loaded = true;
    return 0;
}

int RomLoader::load_binary(const char* filename)
{
    std::ifstream src(filename, std::ios::in | std::ios::binary);
    if (!src)
    {
        std::cout << "cannot open file: " << filename << std::endl;
        loaded = false;
        return 1;
    }

    length = filesize(filename);
    char* buffer = new char[length];
    src.read(buffer, length);
    rom = reinterpret_cast<uint8_t*>(buffer);
    src.close();
    loaded = true;
    return 0;
}

int RomLoader::filesize(const char* filename)
{
    std::ifstream in(filename, std::ifstream::in | std::ifstream::binary);
    in.seekg(0, std::ifstream::end);
    int size = static_cast<int>(in.tellg());
    in.close();
    return size;
}
