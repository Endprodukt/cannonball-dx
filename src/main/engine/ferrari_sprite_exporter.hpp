#pragma once

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/data/ozoom_lookup.hpp"
#include "engine/oentry.hpp"
#include "engine/osprites.hpp"
#include "globals.hpp"
#include "roms.hpp"
#include "video.hpp"

// Temporary development helper used by feature/ferrari-sprite-exporter.
//
// It watches only the player Ferrari plus the dedicated crash-car sprite and
// writes each unique frame/orientation once.  The uniqueness key deliberately
// ignores palette changes, so the extra CannonBall DX car colours do not create
// duplicate dumps.  Every dump contains:
//   - .bmp  : the sprite with the currently mapped in-game palette
//   - .idx  : one byte per source pixel, preserving the original 4-bit value
//   - .json : frame address, flip state, dimensions and ROM source metadata
//
// Delete the ferrari_sprite_export folder before a fresh capture session.
namespace ferrari_sprite_exporter
{
    static constexpr uint32_t WH_TABLE = 0x20000;
    static constexpr uint32_t WORDS_PER_BANK = 0x10000;

    inline std::unordered_set<std::string>& seen_keys()
    {
        static std::unordered_set<std::string> keys;
        return keys;
    }

    inline uint32_t& export_count()
    {
        static uint32_t count = 0;
        return count;
    }

    inline std::filesystem::path output_dir()
    {
        return std::filesystem::path("ferrari_sprite_export");
    }

    inline std::string hex8(uint32_t value)
    {
        std::ostringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << value;
        return ss.str();
    }

    inline void ensure_output_dir()
    {
        static bool initialized = false;
        if (initialized)
            return;

        initialized = true;
        std::error_code ec;
        std::filesystem::create_directories(output_dir(), ec);

        const auto readme = output_dir() / "README.txt";
        if (!std::filesystem::exists(readme))
        {
            std::ofstream out(readme);
            if (out)
            {
                out << "CannonBall DX Ferrari sprite capture\n\n"
                    << "Drive normally and deliberately trigger skids/crashes until the console\n"
                    << "stops reporting new Ferrari sprites.  Files are deduplicated by source\n"
                    << "frame address + HFLIP, not by car colour/palette.\n\n"
                    << ".bmp  = easy-to-view image using the current in-game palette.\n"
                    << ".idx  = exact source pixel indices, one byte per pixel (values 0..15).\n"
                    << ".json = metadata needed to reconstruct/edit the sprite exactly.\n\n"
                    << "Pixel values 0 and 15 are transparent in the sprite renderer.\n"
                    << "Delete this entire folder before starting a completely fresh capture.\n";
            }
        }

        const auto index = output_dir() / "index.csv";
        if (!std::filesystem::exists(index))
        {
            std::ofstream out(index);
            if (out)
                out << "name,kind,frame_addr,hflip,pal_src,pal_dst,bank,offset,pitch,width,height\n";
        }
    }

    inline bool get_source_geometry(
        const oentry* entry,
        uint32_t& frame_meta,
        uint8_t& bank,
        uint16_t& offset,
        uint8_t& pitch,
        uint16_t& width,
        uint16_t& height)
    {
        if (!entry || !roms.rom0p || !roms.rom0p->rom || !roms.sprites.rom)
            return false;

        // Export the largest/native sprite representation (SIZE1), regardless
        // of the current on-screen zoom level.
        frame_meta = entry->addr + SIZE1;
        if (frame_meta + 9 >= roms.rom0p->length)
            return false;

        bank = roms.rom0p->read8(frame_meta + 7);
        pitch = roms.rom0p->read8(frame_meta + 5);
        offset = roms.rom0p->read16(frame_meta + 8);

        if (pitch == 0)
            return false;

        // This mirrors the native SIZE1 dimension lookup used by
        // OSprites::do_sprite(), so the dump is in source-pixel dimensions and
        // is independent of screen resolution, scaler and widescreen mode.
        constexpr uint32_t native_index = 127;
        uint16_t lookup_mask = ZOOM_LOOKUP[(native_index * 4) + 1];
        lookup_mask += 0x4000;

        uint16_t d0 = (lookup_mask & 0xFF00) + roms.rom0p->read8(frame_meta + 1);
        if (WH_TABLE + d0 >= roms.rom0p->length)
            return false;
        width = roms.rom0p->read8(WH_TABLE + d0);

        d0 = (lookup_mask & 0xFF00) + roms.rom0p->read8(frame_meta + 3);
        if (WH_TABLE + d0 >= roms.rom0p->length)
            return false;
        height = roms.rom0p->read8(WH_TABLE + d0);

        // Conservative fallbacks for unusual animation entries.
        if (width == 0)
            width = static_cast<uint16_t>(pitch * 8);
        if (height == 0)
            height = roms.rom0p->read8(frame_meta + 4);

        if (width == 0 || height == 0 || width > 1024 || height > 1024)
            return false;

        return true;
    }

    inline uint32_t sprite_word(uint8_t bank, uint32_t word_offset)
    {
        if (!roms.sprites.rom || roms.sprites.length < 4)
            return 0;

        const uint32_t bank_count = (roms.sprites.length / 4) / WORDS_PER_BANK;
        if (bank_count == 0)
            return 0;

        const uint32_t real_bank = bank % bank_count;
        const uint32_t word_index = (real_bank * WORDS_PER_BANK) + word_offset;
        const uint32_t byte_index = word_index * 4;
        if (byte_index + 3 >= roms.sprites.length)
            return 0;

        const uint8_t* p = roms.sprites.rom + byte_index;
        return (static_cast<uint32_t>(p[3]) << 24) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[1]) << 8) |
               static_cast<uint32_t>(p[0]);
    }

    inline std::vector<uint8_t> decode_indices(
        uint8_t bank,
        uint16_t offset,
        uint8_t pitch,
        uint16_t width,
        uint16_t height,
        bool hflip)
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height, 0);

        for (uint16_t y = 0; y < height; y++)
        {
            const uint32_t row = static_cast<uint32_t>(offset) +
                                 static_cast<uint32_t>(y) * pitch;

            for (uint16_t word = 0; word < pitch; word++)
            {
                const uint32_t packed = sprite_word(bank, row + word);
                const uint8_t values[8] = {
                    static_cast<uint8_t>((packed >> 28) & 0xF),
                    static_cast<uint8_t>((packed >> 24) & 0xF),
                    static_cast<uint8_t>((packed >> 20) & 0xF),
                    static_cast<uint8_t>((packed >> 16) & 0xF),
                    static_cast<uint8_t>((packed >> 12) & 0xF),
                    static_cast<uint8_t>((packed >> 8) & 0xF),
                    static_cast<uint8_t>((packed >> 4) & 0xF),
                    static_cast<uint8_t>(packed & 0xF)
                };

                for (uint16_t i = 0; i < 8; i++)
                {
                    const uint32_t x = static_cast<uint32_t>(word) * 8 + i;
                    if (x >= width)
                        break;
                    pixels[static_cast<size_t>(y) * width + x] = values[i];
                }

                // OutRun marks the end of a sprite row with an F in the
                // second-to-last nibble of the packed word.
                if ((packed & 0x000000F0u) == 0x000000F0u)
                    break;
            }
        }

        if (hflip)
        {
            for (uint16_t y = 0; y < height; y++)
            {
                auto begin = pixels.begin() + static_cast<size_t>(y) * width;
                std::reverse(begin, begin + width);
            }
        }

        return pixels;
    }

    inline void palette_rgb(uint16_t palette_index, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        const uint16_t a = video.read_pal16(static_cast<uint32_t>(palette_index) * 2);
        const uint8_t r5 = static_cast<uint8_t>((((a >> 0) & 0xF) << 1) | ((a >> 12) & 1));
        const uint8_t g5 = static_cast<uint8_t>((((a >> 4) & 0xF) << 1) | ((a >> 13) & 1));
        const uint8_t b5 = static_cast<uint8_t>((((a >> 8) & 0xF) << 1) | ((a >> 14) & 1));
        r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        g = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
        b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    }

    inline bool save_bmp(
        const std::filesystem::path& path,
        const std::vector<uint8_t>& indices,
        uint16_t width,
        uint16_t height,
        uint8_t pal_dst)
    {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
            0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return false;

        if (SDL_LockSurface(surface) != 0)
        {
            SDL_FreeSurface(surface);
            return false;
        }

        for (uint16_t y = 0; y < height; y++)
        {
            auto* dst = reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(surface->pixels) + y * surface->pitch);

            for (uint16_t x = 0; x < width; x++)
            {
                const uint8_t pix = indices[static_cast<size_t>(y) * width + x] & 0xF;

                // BMP has no reliable cross-viewer alpha handling. Use bright
                // magenta for transparent source pixels so the true sprite
                // outline remains obvious while inspecting/editing dumps.
                if (pix == 0 || pix == 15)
                {
                    dst[x] = SDL_MapRGBA(surface->format, 255, 0, 255, 255);
                    continue;
                }

                uint8_t r = 0, g = 0, b = 0;
                const uint16_t palette_index = static_cast<uint16_t>(
                    0x800 + (static_cast<uint16_t>(pal_dst) << 4) + pix);
                palette_rgb(palette_index, r, g, b);
                dst[x] = SDL_MapRGBA(surface->format, r, g, b, 255);
            }
        }

        SDL_UnlockSurface(surface);
        const int result = SDL_SaveBMP(surface, path.string().c_str());
        SDL_FreeSurface(surface);
        return result == 0;
    }

    inline void save_indices(
        const std::filesystem::path& path,
        const std::vector<uint8_t>& indices)
    {
        std::ofstream out(path, std::ios::binary);
        if (out && !indices.empty())
            out.write(reinterpret_cast<const char*>(indices.data()),
                      static_cast<std::streamsize>(indices.size()));
    }

    inline void save_metadata(
        const std::filesystem::path& path,
        const char* kind,
        const oentry* entry,
        bool hflip,
        uint32_t frame_meta,
        uint8_t bank,
        uint16_t offset,
        uint8_t pitch,
        uint16_t width,
        uint16_t height)
    {
        std::ofstream out(path);
        if (!out)
            return;

        out << "{\n"
            << "  \"kind\": \"" << kind << "\",\n"
            << "  \"frame_addr\": \"0x" << hex8(entry->addr) << "\",\n"
            << "  \"frame_meta\": \"0x" << hex8(frame_meta) << "\",\n"
            << "  \"hflip\": " << (hflip ? "true" : "false") << ",\n"
            << "  \"pal_src\": " << entry->pal_src << ",\n"
            << "  \"pal_dst\": " << static_cast<unsigned>(entry->pal_dst) << ",\n"
            << "  \"bank\": " << static_cast<unsigned>(bank) << ",\n"
            << "  \"offset_words\": " << offset << ",\n"
            << "  \"pitch_words\": " << static_cast<unsigned>(pitch) << ",\n"
            << "  \"width\": " << width << ",\n"
            << "  \"height\": " << height << ",\n"
            << "  \"index_format\": \"one byte per pixel, values 0..15; 0 and 15 transparent\"\n"
            << "}\n";
    }

    inline void capture(const oentry* entry, const char* kind)
    {
        if (!entry || entry->addr == 0 || entry->zoom == 0)
            return;
        if (!(entry->control & OSprites::ENABLE))
            return;
        if (entry->pal_dst == 0)
            return;

        const bool hflip = (entry->control & OSprites::HFLIP) != 0;
        const std::string key = std::string(kind) + "_addr_" + hex8(entry->addr) +
                                "_flip_" + (hflip ? "1" : "0");

        ensure_output_dir();

        auto& seen = seen_keys();
        if (seen.find(key) != seen.end())
            return;

        const auto bmp_path = output_dir() / (key + ".bmp");
        if (std::filesystem::exists(bmp_path))
        {
            seen.insert(key);
            return;
        }

        uint32_t frame_meta = 0;
        uint8_t bank = 0;
        uint16_t offset = 0;
        uint8_t pitch = 0;
        uint16_t width = 0;
        uint16_t height = 0;

        if (!get_source_geometry(entry, frame_meta, bank, offset, pitch, width, height))
            return;

        const auto indices = decode_indices(bank, offset, pitch, width, height, hflip);
        if (indices.empty())
            return;

        // Insert only after successful decoding, so a transient not-yet-ready
        // palette/ROM state can be retried on the next frame.
        seen.insert(key);

        save_bmp(bmp_path, indices, width, height, entry->pal_dst);
        save_indices(output_dir() / (key + ".idx"), indices);
        save_metadata(output_dir() / (key + ".json"), kind, entry, hflip,
                      frame_meta, bank, offset, pitch, width, height);

        std::ofstream index(output_dir() / "index.csv", std::ios::app);
        if (index)
        {
            index << key << ',' << kind << ",0x" << hex8(entry->addr) << ','
                  << (hflip ? 1 : 0) << ',' << entry->pal_src << ','
                  << static_cast<unsigned>(entry->pal_dst) << ','
                  << static_cast<unsigned>(bank) << ',' << offset << ','
                  << static_cast<unsigned>(pitch) << ',' << width << ',' << height << '\n';
        }

        const uint32_t count = ++export_count();
        std::cout << "[Ferrari Sprite Exporter] new " << kind << " frame: "
                  << key << " (" << count << " this session)" << std::endl;
    }

    inline void tick(const oentry* ferrari)
    {
        capture(ferrari, "normal");
        capture(&osprites.jump_table[OSprites::SPRITE_CRASH], "crash");
    }
}
