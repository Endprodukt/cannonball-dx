/***************************************************************************
    Load OutRun ROM Set.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <iostream>
#include <cstring>
#include <string>
#include "stdint.hpp"
#include "roms.hpp"
#include <iostream>

Roms roms;

Roms::Roms()
{
    jap_rom_status = -1;
    rom0p = NULL;
    rom1p = NULL;
}

Roms::~Roms(void)
{
}

// Tidier way to address pointer to member function
// Expanded example: (rom0.*(rom0.load))("epr-10380b.133", 0x00000, 0x10000, 0x1f6cadad, RomLoader::INTERLEAVE2);
#define LOAD(rom, args) (rom.*(rom.load)) args

bool Roms::load_revb_roms(bool fixed_rom)
{
    // If incremented, a rom has failed to load.
    int status = 0;

    // Load Master CPU ROMs
    rom0.init(0x40000);
    status += LOAD(rom0, ("epr-10380b.133", 0x00000, 0x10000, 0x1f6cadad, RomLoader::INTERLEAVE2, VERBOSE));
    status += LOAD(rom0, ("epr-10382b.118", 0x00001, 0x10000, 0xc4c3fa1a, RomLoader::INTERLEAVE2, VERBOSE));
    status += LOAD(rom0, ("epr-10381b.132", 0x20000, 0x10000, 0xbe8c412b, RomLoader::INTERLEAVE2, VERBOSE));
    status += LOAD(rom0, ("epr-10383b.117", 0x20001, 0x10000, 0x10a2014a, RomLoader::INTERLEAVE2, VERBOSE));

    // Load Slave CPU ROMs
    rom1.init(0x40000);
    status += LOAD(rom1, ("epr-10327a.76", 0x00000, 0x10000, 0xe28a5baf, RomLoader::INTERLEAVE2, VERBOSE));
    status += LOAD(rom1, ("epr-10329a.58", 0x00001, 0x10000, 0xda131c81, RomLoader::INTERLEAVE2, VERBOSE));
    status += LOAD(rom1, ("epr-10328a.75", 0x20000, 0x10000, 0xd5ec5e5d, RomLoader::INTERLEAVE2, VERBOSE));
    status += LOAD(rom1, ("epr-10330a.57", 0x20001, 0x10000, 0xba9ec82a, RomLoader::INTERLEAVE2, VERBOSE));

    // Load Non-Interleaved Tile ROMs
    tiles.init(0x30000);
    status += LOAD(tiles, ("opr-10268.99", 0x00000, 0x08000, 0x95344b04, RomLoader::NORMAL, VERBOSE));
    status += LOAD(tiles, ("opr-10232.102", 0x08000, 0x08000, 0x776ba1eb, RomLoader::NORMAL, VERBOSE));
    status += LOAD(tiles, ("opr-10267.100", 0x10000, 0x08000, 0xa85bb823, RomLoader::NORMAL, VERBOSE));
    status += LOAD(tiles, ("opr-10231.103", 0x18000, 0x08000, 0x8908bcbf, RomLoader::NORMAL, VERBOSE));
    status += LOAD(tiles, ("opr-10266.101", 0x20000, 0x08000, 0x9f6f1a74, RomLoader::NORMAL, VERBOSE));
    status += LOAD(tiles, ("opr-10230.104", 0x28000, 0x08000, 0x686f5e50, RomLoader::NORMAL, VERBOSE));

    // Load Non-Interleaved Road ROMs (2 identical roms, 1 for each road)
    road.init(0x10000);
    status += LOAD(road, ("opr-10185.11", 0x000000, 0x08000, 0x22794426, RomLoader::NORMAL, VERBOSE));
    status += LOAD(road, ("opr-10186.47", 0x008000, 0x08000, 0x22794426, RomLoader::NORMAL, VERBOSE));

    // Load Interleaved Sprite ROMs
    sprites.init(0x100000);
    status += LOAD(sprites, ("mpr-10371.9", 0x000000, 0x20000, 0x7cc86208, RomLoader::INTERLEAVE4, VERBOSE));
    status += LOAD(sprites, ("mpr-10373.10", 0x000001, 0x20000, 0xb0d26ac9, RomLoader::INTERLEAVE4, VERBOSE));
    status += LOAD(sprites, ("mpr-10375.11", 0x000002, 0x20000, 0x59b60bd7, RomLoader::INTERLEAVE4, VERBOSE));
    status += LOAD(sprites, ("mpr-10377.12", 0x000003, 0x20000, 0x17a1b04a, RomLoader::INTERLEAVE4, VERBOSE));
    status += LOAD(sprites, ("mpr-10372.13", 0x080000, 0x20000, 0xb557078c, RomLoader::INTERLEAVE4, VERBOSE));
    status += LOAD(sprites, ("mpr-10374.14", 0x080001, 0x20000, 0x8051e517, RomLoader::INTERLEAVE4, VERBOSE));
    status += LOAD(sprites, ("mpr-10376.15", 0x080002, 0x20000, 0xf3b8f318, RomLoader::INTERLEAVE4, VERBOSE));
    status += LOAD(sprites, ("mpr-10378.16", 0x080003, 0x20000, 0xa1062984, RomLoader::INTERLEAVE4, VERBOSE));

    // Load Z80 Sound ROM
    // Note: This is a deliberate decision to double the Z80 ROM Space to accomodate extra FM based music
    z80.init(0x10000);
    status += LOAD(z80, ("epr-10187.88", 0x0000, 0x08000, 0xa10abaa9, RomLoader::NORMAL, VERBOSE));

    // Load Sega PCM Chip Samples
    pcm.init(0x60000);
    status += LOAD(pcm, ("opr-10193.66", 0x00000, 0x08000, 0xbcd10dde, RomLoader::NORMAL, VERBOSE));
    status += LOAD(pcm, ("opr-10192.67", 0x10000, 0x08000, 0x770f1270, RomLoader::NORMAL, VERBOSE));
    status += LOAD(pcm, ("opr-10191.68", 0x20000, 0x08000, 0x20a284ab, RomLoader::NORMAL, VERBOSE));
    status += LOAD(pcm, ("opr-10190.69", 0x30000, 0x08000, 0x7cab70e2, RomLoader::NORMAL, VERBOSE));
    status += LOAD(pcm, ("opr-10189.70", 0x40000, 0x08000, 0x01366b54, RomLoader::NORMAL, VERBOSE));

    // The final PCM ROM exists in three useful forms. load_pcm_rom() prefers
    // Sega/M2's official corrected dump, retains CannonBall's historical
    // patched ROM, and finally falls back to the original faulty arcade dump.
    status += load_pcm_rom(fixed_rom);

    // If status has been incremented, a rom has failed to load.
    return status == 0;
}

bool Roms::load_japanese_roms()
{
    // Only attempt to initalize the arrays once.
    if (jap_rom_status == -1)
    {
        j_rom0.init(0x40000);
        j_rom1.init(0x40000);
    }

    // If incremented, a rom has failed to load.
    jap_rom_status = 0;

    // Load Master CPU ROMs
    jap_rom_status += LOAD(j_rom0, ("epr-10380.133", 0x00000, 0x10000, 0xe339e87a, RomLoader::INTERLEAVE2, VERBOSE));
    jap_rom_status += LOAD(j_rom0, ("epr-10382.118", 0x00001, 0x10000, 0x65248dd5, RomLoader::INTERLEAVE2, VERBOSE));
    jap_rom_status += LOAD(j_rom0, ("epr-10381.132", 0x20000, 0x10000, 0xbe8c412b, RomLoader::INTERLEAVE2, VERBOSE));
    jap_rom_status += LOAD(j_rom0, ("epr-10383.117", 0x20001, 0x10000, 0xdcc586e7, RomLoader::INTERLEAVE2, VERBOSE));

    // Load Slave CPU ROMs
    jap_rom_status += LOAD(j_rom1, ("epr-10327.76", 0x00000, 0x10000, 0xda99d855, RomLoader::INTERLEAVE2, VERBOSE));
    jap_rom_status += LOAD(j_rom1, ("epr-10329.58", 0x00001, 0x10000, 0xfe0fa5e2, RomLoader::INTERLEAVE2, VERBOSE));
    jap_rom_status += LOAD(j_rom1, ("epr-10328.75", 0x20000, 0x10000, 0x3c0e9a7f, RomLoader::INTERLEAVE2, VERBOSE));
    jap_rom_status += LOAD(j_rom1, ("epr-10330.57", 0x20001, 0x10000, 0x59786e99, RomLoader::INTERLEAVE2, VERBOSE));
    // If status has been incremented, a rom has failed to load.
    return jap_rom_status == 0;
}

int Roms::load_pcm_rom(bool fixed_rom)
{
    int status = 1;

    if (fixed_rom)
    {
        // Prefer the official Sega/M2 corrected ROM used by current MAME
        // enhanced sets. CRC matching means it may be named differently or
        // live inside a merged ZIP archive.
        status = LOAD(pcm, ("enhanced_203_opr-10188.71", 0x50000, 0x08000, 0xC2DE09B2, RomLoader::NORMAL, false));

        // Backwards compatibility: CannonBall's historical bspatch output.
        if (status == 1)
            status = LOAD(pcm, ("opr-10188.71f", 0x50000, 0x08000, 0x37598616, RomLoader::NORMAL, false));

        if (status == 1)
        {
            std::cerr << "WARNING: config.fix_samples is set, but no corrected PCM ROM was found. RevB ROM will be used." << std::endl;
            fixed_rom = false;
        }
    }

    // Original OutRun PCM ROM with the known stuck data bit. Keep this as the
    // final fallback so every legacy extracted Rev B set continues to work.
    if (!fixed_rom)
        status = LOAD(pcm, ("opr-10188.71", 0x50000, 0x08000, 0xbad30ad9, RomLoader::NORMAL, VERBOSE));

    return status;
}

bool Roms::load_ym_data(const char* filename)
{
    RomLoader data;
    if (data.load_binary(filename) != 0)
        return false;

    if (data.length > 0x8000)
    {
        std::cout << "External music data is too large (max 32K): "
                  << filename << std::endl;
        data.unload();
        return false;
    }

    // Legacy .ym files already use CannonBall's external music layout.
    // Keep their loading behaviour completely unchanged.
    std::string name(filename);
    std::size_t dot = name.find_last_of('.');
    bool is_bin = dot != std::string::npos &&
                  (name.substr(dot) == ".bin" || name.substr(dot) == ".BIN");

    if (!is_bin)
    {
        memset(z80.rom + 0x8000, 0, 0x8000);
        memcpy(z80.rom + 0x8000, data.rom, data.length);
        data.unload();
        return true;
    }

    const uint16_t LOAD_BASE    = 0x8000;
    const uint16_t CUSTOM_DATA  = 0x84B9;
    const uint16_t CUSTOM_OFF   = CUSTOM_DATA - LOAD_BASE;

    // Native CannonBall / 3DS / Switch BIN files are already assembled for
    // the external Z80 area. They begin with the $84B9 custom-data pointer and
    // must be copied byte-for-byte so existing files remain fully compatible.
    if (data.length >= 2 && data.rom[0] == 0xB9 && data.rom[1] == 0x84)
    {
        memset(z80.rom + LOAD_BASE, 0, 0x8000);
        memcpy(z80.rom + LOAD_BASE, data.rom, data.length);
        data.unload();
        return true;
    }

    // Raw siMMpLified binaries are normally assembled directly over an
    // original OutRun song (for example Magical Sound Shower at $3D5F).
    // Their first word points to tuneHeaders, which immediately follows the
    // eight-byte master header. Use that relationship to recover the original
    // ORG and relocate only pointers that refer back into this binary itself.
    if (data.length < 35)
    {
        std::cout << "Invalid custom BIN music file: " << filename << std::endl;
        data.unload();
        return false;
    }

    uint16_t tune_headers = data.rom[0] | (data.rom[1] << 8);
    if (tune_headers < 8)
    {
        std::cout << "Invalid siMMpLified BIN header: " << filename << std::endl;
        data.unload();
        return false;
    }

    uint16_t source_base = tune_headers - 8;
    uint32_t source_end  = static_cast<uint32_t>(source_base) + data.length;

    // siMMpLified's master header is followed by a 13-track pointer table.
    // Requiring that layout avoids treating arbitrary/corrupt BIN files as
    // relocatable songs.
    if (data.rom[8] != 13 || source_end > 0x10000)
    {
        std::cout << "Unrecognized custom BIN music format: " << filename << std::endl;
        data.unload();
        return false;
    }

    for (int i = 0; i < 13; i++)
    {
        int offset = 9 + (i * 2);
        uint16_t ptr = data.rom[offset] | (data.rom[offset + 1] << 8);
        if (ptr < source_base || ptr >= source_end)
        {
            std::cout << "Invalid siMMpLified track pointer in: " << filename << std::endl;
            data.unload();
            return false;
        }
    }

    if (data.length > (0x8000 - CUSTOM_OFF))
    {
        std::cout << "siMMpLified music data is too large for relocation: "
                  << filename << std::endl;
        data.unload();
        return false;
    }

    memset(z80.rom + LOAD_BASE, 0, 0x8000);

    // Match the native external layout expected by CannonBall.
    z80.rom[LOAD_BASE]     = 0xB9;
    z80.rom[LOAD_BASE + 1] = 0x84;
    memcpy(z80.rom + CUSTOM_DATA, data.rom, data.length);

    int relocated = 0;
    for (int i = 0; i < data.length - 1; i++)
    {
        uint16_t ptr = data.rom[i] | (data.rom[i + 1] << 8);
        if (ptr >= source_base && ptr < source_end)
        {
            uint16_t new_ptr = CUSTOM_DATA + (ptr - source_base);
            z80.rom[CUSTOM_DATA + i]     = new_ptr & 0xFF;
            z80.rom[CUSTOM_DATA + i + 1] = new_ptr >> 8;
            relocated++;
        }
    }

    std::cout << "Loaded raw siMMpLified BIN music (relocated "
              << relocated << " pointers): " << filename << std::endl;

    data.unload();
    return true;
}
