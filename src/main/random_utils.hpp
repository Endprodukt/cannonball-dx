#pragma once

#include <random>

namespace random_utils
{
    // Create an independent engine from both platform entropy and a
    // high-resolution clock. Arcade-compatible RNG paths remain untouched.
    std::mt19937 make_seeded_engine();
}
