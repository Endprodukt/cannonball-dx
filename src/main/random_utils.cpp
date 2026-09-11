#include "random_utils.hpp"

#include <chrono>
#include <cstdint>

std::mt19937 random_utils::make_seeded_engine()
{
    std::random_device entropy;
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());

    std::seed_seq seed
    {
        entropy(),
        entropy(),
        static_cast<uint32_t>(now),
        static_cast<uint32_t>(now >> 32),
    };

    return std::mt19937(seed);
}
