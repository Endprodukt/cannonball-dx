#pragma once

#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/outrun.hpp"
#include "video.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace music_side_art
{
    inline constexpr std::array<uint8_t, 32> STANDARD_DAC = {
        0, 8, 16, 24, 31, 39, 47, 55,
        62, 70, 78, 86, 94, 102, 109, 117,
        125, 133, 140, 148, 156, 164, 171, 179,
        187, 195, 203, 211, 218, 226, 234, 242
    };

    struct Rgb
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    struct Span
    {
        uint8_t y;
        uint8_t x;
        uint8_t length;
        uint8_t colour;
    };

    struct State
    {
        bool decoded = false;
        bool valid = false;
        bool logged = false;
        std::vector<Rgb> colours;
        std::vector<Span> spans;
        std::vector<uint16_t> palette_indices;
    };

    // This is the edited 536x224 Music Select image reduced to only the two
    // 68-pixel side strips. RGB 0,140,242 (the old blank blue fill) is omitted.
    // Keeping the 4.5 KB payload in the executable avoids a separate resource
    // file being missed when only the freshly built EXE is copied for testing.
    inline constexpr const char* ART_BASE64 =
        "TVMyMQETUgQAqwAAu6sA2qsA6rtOTk6MjJyMnH2cjH2cnJycnKurnIyrq8urq+q7q5y7y+rLy8vLy+rL2urq6upwDwERcBADEnATARFwFAEOcDABEnAxAhFxDQEOcQ4EEnESARFxEwIScRUBEXEWAQ5xGAIScRoBDnEoAxJxLAIScS4CEXEwAxJxMwIRcTUBEnE2ARFxSwERcUwDEnFPARFxUAEOcgoBDnILAhJyDQERcg4BEnIPAxFyEgESchMBEXIUARJyFQERchYBEnIXBRFyHAMSch8BDnIlAQxyJgIOcigBEnIpAg5yKwERciwBDHItBA5yMQMScjQCEXI2ARJyNwIOckkBDnJKBBJyTgERck8CEnJRARFyUgEOclQBEnJVAg5ydwEScngBEXMJAgxzCwIOcw0BEnMOAg5zEAMScxMCDnMVARJzFgERcxcEDnMbAhFzHQEOcx4GDHMmBgxzLQQMczEFDnM2BgxzRgEOc0cCEnNJARFzSgESc0sDEXNOARJzTwERc1ABEnNRARFzUgESc1MFEXNYAxJzWwEOc28CEnNyAhJzdAMRc3cCEnN5AhFzewESc3wBEXQLCQx0FgkMdC0BDHQxBQx0RAMMdEcCDnRJARJ0SgIOdEwDEnRPAg50UQESdFIBEXRTBA50VwIRdFkBDnRaBgx0awEMdGwDDnRvARJ0cAEOdHEBEXRyAQx0cwUOdHgCEnR6AhF0fAESdH0DDnVHCQx1UgkMdWwGDHVzBQx1eAQOdXwGDHZzAQx2eAQMfEYBDXxHAQZ8SAIHfUYCBn1IAQp9SQINfUsBB35EAwZ+RwEKfkgCBn5KAQp+SwMGfk4CCn5QAQ1+UwEGflQBB35dAwZ+bAIGfm4BB39EAgZ/RgIKf0gBBn9JAQd/SgEGf0sBDX9MBwZ/UwEKf1QBDX9VAwd/WAIKf1oBDX9bAQp/XAENf10GCn9nBQd/bAEGf20BCn9uAQd/bwUKf3QBDX91AgqARAIGgEYCCoBIAQaASQIHgEsBBoBMAgqATgYGgFQBCoBVAg2AVwEKgFgCB4BaAQaAWwENgFwBCoBdAweAYAIKgGICBoBkBgeAagEKgGsBBoBsAgqAbgMHgHEBCoByAQaAcwIKgHUBDYB2AwqAeQIHgUQCBoFGAwqBSQEGgUoEB4FOAQaBTwEKgVAEBoFUBQeBWQQKgV0EB4FhAwaBZAEKgWUBBoFmAQeBZwYKgW0BBoFuAweBcQMGgXQCCoF2BAeBegEGgXsCCoF9AgaBfwMHgkQCBoJGAwqCSQEGgkoEB4JOAQaCTwUKglQBBoJVAwqCWAEHglkCCoJbAQaCXAEKgl0CB4JfAQqCYAIHgmICBoJkBgqCagEGgmsBCoJsAgaCbgUHgnMCBoJ1AQqCdgUHgnsDCoJ+BAeCggEGgoMCCoKFAQ2ChgIGg0QFCoNJAweDTAIKg04CB4NQAgqDUgEHg1MCCoNVAgaDVwEKg1gEBoNcAQeDXQMGg2ACB4NiAwaDZQEKg2YBBoNnBAqDawIGg20MB4N5AgqDewIGg30GB4ODAQqDhAENg4UBB4OGAgaERAMKhEcDBoRKBAqETgIHhFABCoRRAQeEUgcKhFkHBoRgAgeEYgMGhGUBCoRmAQaEZwQKhGsBBoRsCAeEdAYKhHoDBoR9BQeEggEGhIMBDYSEAQeEhQIGhIcBCoVEAgqFRgEHhUcBCoVIAgaFSgEAhUsDBoVOBAeFUgIKhVQBB4VVAgqFVwIHhVkBCoVaAgaFXAEAhV0CBoVfAQqFYAQHhWQFBoVpAQeFagEGhWsIB4VzAgqFdQIAhXcDBoV6AgCFfAYHhYIBCoWDAgaFhQIKhYcBB4ZEAgaGRgMHhkkBCoZKAQaGSwMAhk4EBoZSAQqGUwIHhlUDBoZYAweGWwQAhl8GB4ZlAgCGZwIGhmkEB4ZtAQaGbgMKhnEEB4Z1AQaGdgMAhnkBBoZ6CQeGgwEGhoQDAIaHAQeHQQMEh0QCB4dGAQaHRwEAh0gCBodKAgCHTAIGh04CAIdQAQaHUQEKh1IBB4dTBgaHWQMHh1wBAIddAweHYAEKh2ECB4djAgCHZQEKh2YFB4drAgaHbQQKh3EBB4dyAQqHcwMAh3YNB4eDAwCHhgEGh4cBB4gAPgGIPgYEiEREAYkAHQGJHQESiR4IAYkmARKJJw8BiTYBEok3AwGJOgoEiUQEAYlIARKJSREBiVoBEolbGQGJdAESiXUTAYoADQKKDQESig4IAooWARKKFxgCii8BEoowBwKKNw0EikQPAopTARKKVBACimQBEoplGgKKfwESioAIAosANAKLNBAEi0REAowAAQKMAQESjAIwAowyEgSMREECjIUBEoyGAgKNADEDjTETBI1ERAOOAC8Dji8VBI5ERAOPAC4Djy4WBI9ERAOQACADkCAMC5AsGASQREQDkQAZA5EZBwuRIAsQkSsZBJFERAOSABYDkhYDC5IZEBCSKRsEkkREA5MAEQOTEQULkxYMEJMiBhKTKBwEk0REEpQADgOUDgMLlBEIEJQZDhKUJxkElEAEEJRERBKVAAsDlQsDC5UOCRCVFwISlRkLEJUkAhKVJhcElT0HC5VERASWAAkDlgkCC5YLCRCWFAMSlhcCEJYZBguWHwUQliQBEpYlFQSWOgoLlkRED5cABwOXBwILlwkIEJcRAxKXFAMQlxcCC5cZBAmXHQILlx8FEJckFASXOAwJl0RED5gABAOYBAMLmAcHEJgOAxKYEQMQmBQDC5gXAgmYGQIFmBsDCZgeAQuYHwQQmCMUBJg3DQWYREQPmQACA5kCAguZBAcQmQsDEpkOAxCZEQMLmRQDCZkXBgWZHQEJmR4BC5kfAxCZIhMEmTUPBZlERA+aAAILmgIGEJoIAxKaCwMQmg4DC5oRAwmaFAkFmh0BCZoeAguaIAEQmiETBJo0EAWaREQPmwAGEJsGAhKbCAMQmwsDC5sOAwmbEQ0Fmx4CC5sgARCbIREEmzISBZtERA+cAAQQnAQCEpwGAhCcCAMLnAsDCZwOEAWcHgILnCARBJwxEwWcREQPnQABEJ0BAxKdBAIQnQYCC50IAwmdCxMFnR4BC50fEQSdMAIFnTIDCZ01DwWdREQPngABEp4BAxCeBAILngYCCZ4IFgWeHgELnh8QBJ4vBgWeNQEJnjYOBZ5ERA+fAAEQnwEDC58EAgmfBhgFnx4QBJ8uCAWfNgIJnzgMBZ9ERA+gAAELoAEDCaAEGgWgHhAEoC4KBaA4AQmgOQsFoEREEqEAAgmhAhsFoR0QBKEtDAWhOQMJoTwIBaFERBCiAAEJogEcBaIdDwSiLBAFojwBCaI9BwWiREQLowAdBaMdDwSjLBEFoz0CCaM/BQWjREQLpAAcBaQcDwSkKwEJpCwTBaQ/AQmkQAQFpEREC6UAHAWlHA8EpSsBCaUsFAWlQAIJpUICBaVERAumABwFphwOBKYqAQumKwEJpiwWBaZCAgmmREQLpwAcBaccDgSnKgELpysBCacsGAWnREQLqAAbBagbDQSoKAMIqCsBC6gsGAWoREQLqQAbBakbDQSpKAEIqSkCBKkrAgipLRcFqUREC6oAGwWqGw0EqigBCKopBASqLQIIqi8VBapERAurABsFqxsNBKsoAQirKQYEqy8BC6swFAWrREQLrAAaBawaDgSsKAEIrCkGBKwvAQisMAILrDISBaxERAutABoFrRoOBK0oAQitKQYErS8DCK0yAwutNQ8FrUREC64AGgWuGg0EricBCK4oBgSuLgcIrjUCC643DQWuREQLrwAaBa8aDQSvJwEIrygGBK8uCQivNwILrzkLBa9ERAuwABoFsBoNBLAnAQiwKAYEsC4LCLA5BQuwPgYFsEREC7EAGgWxGgwEsSYBCLEnBgSxLREIsT4EC7FCAgWxREQLsgAZBbIZDQSyJgEIsicGBLItFQiyQkYLswAZBbMZDQSzJgEIsycGBLMtFwizREQLtAAZBbQZDQS0JgIItCgFBLQtFwi0REQLtQAZBbUZDAS1JQEItSYBBLUnAgi1KQMEtSwYCLVERAu2ABkFthkMBLYlAQi2JgMEtikbCLZERAu3ABkFtxkMBLclAQi3JgUEtysZCLdERAu4ABkFuBkNBLgmBgi4LAkEuDUPCLhERAu5ABkFuRkNBLkmAQm5JwQQuSsBErksARC5LQILuS8BCbkwBQW5NQUEuToKCLlERAu6ABkFuhkNBLomAQm6JwQQuisBErosARC6LQILui8BCbowCgW6OgQEuj4GCLpERAu7ABkFuxkNBLsmAQm7JwQQuysBErssARC7LQILuy8BCbswDgW7PgMEu0EDCLtERAu8ABkFvBkNBLwmAQm8JwQQvCsBErwsARC8LQILvC8BCbwwEQW8QQMEvEREC70AGQW9GQ4EvScBCb0oBBC9LAESvS0BEL0uAgu9MAEJvTETBb1ERAu+ABkFvhkOBL4nAQm+KAQQviwBEr4tARC+LgILvjABCb4xEwW+REQLvwAaBb8aDQS/JwEJvygEEL8sARK/LQEQvy4CC78wAQm/MRMFv0REC8AAGgXAGg0EwCcBCcAoBBDALAESwC0BEMAuAgvAMAEJwDETBcBERAvBABsFwRsJBMEkARLBJQMEwSgBCcEpAxDBLAESwS0BEMEuAgvBMAEJwTETBcFERAvCABsFwhsJBMIkARLCJQMEwigBCcIpAxDCLAESwi0BEMIuAgvCMAEJwjETBcJERAvDABsFwxsNBMMoAQnDKQMQwywBEsMtARDDLgILwzABCcMxEwXDREQLxAAcBcQcDATEKAEJxCkDEMQsARLELQEQxC4CC8QwAQnEMRMFxEREC8UAHAXFHAwExSgBCcUpAxDFLAESxS0BEMUuAgvFMAEJxTETBcVERBDGABwFxhwJBMYlARLGJgMExikCCcYrAhDGLQESxi4BEMYvAgvGMQEJxjISBcZERBDHAB0Fxx0IBMclARLHJgMExykCCccrAhDHLQESxy4BEMcvAgvHMQEJxzISBcdERBLIAB0FyB0JBMgmARLIJwIEyCkCCcgrAhDILQESyC4BEMgvAgvIMQEJyDJWBckAHQXJHQkEySYBEsknAgTJKQIJySsCEMktARLJLgEQyS8CC8kxAQnJMlYFygAeBcoeCATKJgESyicCBMopAgnKKwIQyi0BEsouARDKLwILyjEBCcoyVgXLAB4Fyx4JBMsnARLLKAMEyysCEMstARLLLgEQyy8CC8sxAQnLMlYFzAAfBcwfDATMKwIQzC0BEswuARDMLwILzDEBCcwyVgXNAB8FzR8NBM0sARDNLQESzS4BEM0vAgvNMQEJzTJWBc4AHwXOHw4Ezi0BEs4uAhDOMAILzjJWBc8AIAXPIA4Ezy4BEs8vARDPMAILzzJWBdAAIAXQIAsE0CsBEtAsAwTQLxUL0EREBdEAIQXRIQsE0SwBEtEtAwTRMBQQ0UREBdIAIQXSIQsE0iwCEtIuAwTSMRMQ0kREBdMAIgXTIgsE0y0CEtMvAwTTMhIS00REBdQAIgXUIgwE1C4CEtQwBATUNBAQ1EREBdUAIwXVIwwE1S8CEtUxBATVNQ8L1UREBdYAIwXWIw4E1jEBEtYyBATWNg4L1kREBdcAJAXXJBME1zcNC9dERAXYAAMJ2AMjBdgmDgTYNAES2DUEBNg5CwjYREQF2QADC9kDCAnZCxsF2SYPBNk1ARLZNgQE2ToKCNlERAXaAAMQ2gMIC9oLGgnaJQIF2icQBNo3ARLaOAUE2j0HCNpERAXbAAML2wMIENsLGgvbJQMJ2ygQBNs4AhLbOgUE2z8FCNtERAXcAAMI3AMIC9wLGhDcJQQL3CkQBNw5BBLcPQQE3EEDCNxERAXdAAsI3QsaC90lBhDdKxEE3TwDEt0/BQTdREQF3gAlCN4lBwveLBEE3j0EEt5BAwTeREQF3wAtCN8tEgTfPwUS30REBQ==";

    inline int decode_value(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    inline std::vector<uint8_t> decode_base64()
    {
        std::vector<uint8_t> out;
        uint32_t accumulator = 0;
        int bits = 0;

        for (const char* p = ART_BASE64; *p; ++p)
        {
            if (*p == '=')
                break;

            const int value = decode_value(*p);
            if (value < 0)
                continue;

            accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
            bits += 6;

            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFFu));
            }
        }

        return out;
    }

    inline State& get_state()
    {
        static State state;
        return state;
    }

    inline bool decode_art()
    {
        State& state = get_state();
        if (state.decoded)
            return state.valid;

        state.decoded = true;
        const std::vector<uint8_t> data = decode_base64();

        if (data.size() < 8 ||
            data[0] != 'M' || data[1] != 'S' ||
            data[2] != '2' || data[3] != '1' || data[4] != 1)
        {
            std::cerr << "Invalid embedded 21:9 Music Select side artwork." << std::endl;
            return false;
        }

        const uint8_t colour_count = data[5];
        const uint16_t span_count =
            static_cast<uint16_t>(data[6]) |
            (static_cast<uint16_t>(data[7]) << 8);
        const std::size_t expected_size =
            8u + static_cast<std::size_t>(colour_count) * 3u +
            static_cast<std::size_t>(span_count) * 4u;

        if (colour_count == 0 || data.size() != expected_size)
        {
            std::cerr << "Invalid embedded 21:9 Music Select side artwork size." << std::endl;
            return false;
        }

        std::size_t offset = 8;
        state.colours.reserve(colour_count);
        for (uint8_t i = 0; i < colour_count; ++i)
        {
            state.colours.push_back({
                data[offset], data[offset + 1], data[offset + 2]
            });
            offset += 3;
        }

        state.spans.reserve(span_count);
        for (uint16_t i = 0; i < span_count; ++i)
        {
            const Span span {
                data[offset], data[offset + 1],
                data[offset + 2], data[offset + 3]
            };
            offset += 4;

            if (span.y >= S16_HEIGHT || span.x >= 136 ||
                span.length == 0 ||
                static_cast<int>(span.x) + span.length > 136 ||
                span.colour >= colour_count)
            {
                std::cerr << "Invalid embedded 21:9 Music Select side artwork span." << std::endl;
                return false;
            }

            state.spans.push_back(span);
        }

        state.palette_indices.resize(colour_count);
        state.valid = true;
        return true;
    }

    inline Rgb palette_rgb(uint16_t palette_index)
    {
        const uint16_t raw = video.read_pal16(
            S16_PALETTE_BASE + static_cast<uint32_t>(palette_index) * 2u);

        const uint8_t r5 = static_cast<uint8_t>(
            (((raw >> 0) & 0x000Fu) << 1) | ((raw >> 12) & 1u));
        const uint8_t g5 = static_cast<uint8_t>(
            (((raw >> 4) & 0x000Fu) << 1) | ((raw >> 13) & 1u));
        const uint8_t b5 = static_cast<uint8_t>(
            (((raw >> 8) & 0x000Fu) << 1) | ((raw >> 14) & 1u));

        return { STANDARD_DAC[r5], STANDARD_DAC[g5], STANDARD_DAC[b5] };
    }

    inline int colour_distance(const Rgb& a, const Rgb& b)
    {
        const int dr = static_cast<int>(a.r) - b.r;
        const int dg = static_cast<int>(a.g) - b.g;
        const int db = static_cast<int>(a.b) - b.b;
        return dr * dr + dg * dg + db * db;
    }

    inline int map_palette()
    {
        State& state = get_state();
        int exact_matches = 0;

        for (std::size_t colour = 0; colour < state.colours.size(); ++colour)
        {
            const Rgb target = state.colours[colour];
            int best_distance = std::numeric_limits<int>::max();
            uint16_t best_index = 0;
            bool exact = false;

            // Remap while the screen is active rather than caching the first
            // GS_MUSIC frame. The music palette is still being established
            // around the state transition, and duplicate palette entries may
            // change later during fades/animation.
            for (uint16_t index = 0; index < 0x1000u; ++index)
            {
                const Rgb candidate = palette_rgb(index);
                const int distance = colour_distance(candidate, target);

                if (distance < best_distance)
                {
                    best_distance = distance;
                    best_index = index;
                }

                if (distance == 0)
                {
                    exact = true;
                    break;
                }
            }

            state.palette_indices[colour] = best_index;
            if (exact)
                ++exact_matches;
        }

        return exact_matches;
    }

    inline void render(uint16_t* buffer)
    {
        if (!buffer || config.video.widescreen != 2 ||
            (outrun.game_state != GS_INIT_MUSIC && outrun.game_state != GS_MUSIC))
        {
            return;
        }

        if (!decode_art())
            return;

        State& state = get_state();
        const int exact_matches = map_palette();

        if (!state.logged)
        {
            std::cout
                << "Embedded Music Select 21:9 side art active: "
                << state.spans.size() << " spans, "
                << exact_matches << "/" << state.colours.size()
                << " palette colours matched exactly."
                << std::endl;
            state.logged = true;
        }

        const bool hires = config.video.hires != 0;
        constexpr int SIDE_WIDTH = 68;
        constexpr int RIGHT_START = S16_WIDTH_ULTRAWIDE - SIDE_WIDTH;

        for (const Span& span : state.spans)
        {
            const int logical_start_x =
                span.x < SIDE_WIDTH
                    ? span.x
                    : RIGHT_START + (span.x - SIDE_WIDTH);
            const uint16_t pixel = state.palette_indices[span.colour];

            for (int dx = 0; dx < span.length; ++dx)
            {
                const int logical_x = logical_start_x + dx;

                if (!hires)
                {
                    buffer[static_cast<int>(span.y) * config.s16_width + logical_x] = pixel;
                }
                else
                {
                    const int physical_x = logical_x << 1;
                    const int physical_y = static_cast<int>(span.y) << 1;
                    uint16_t* dst =
                        buffer + physical_y * config.s16_width + physical_x;

                    dst[0] = pixel;
                    dst[1] = pixel;
                    dst[config.s16_width] = pixel;
                    dst[config.s16_width + 1] = pixel;
                }
            }
        }
    }
}
