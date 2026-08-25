#pragma once

// Preserve the original public API while also declaring private-compatible
// base entry points that the DX wrapper can delegate to. The macro is applied
// only while parsing the preserved header.
#define init(...) init_base(__VA_ARGS__); void init(__VA_ARGS__)
#define tick(...) tick_base(__VA_ARGS__); void tick(__VA_ARGS__)
#define display_scores(...) display_scores_base(__VA_ARGS__); void display_scores(__VA_ARGS__)
#define score_position(...) score_position_base(__VA_ARGS__); int score_position(__VA_ARGS__)
#include "ohiscore_base.hpp"
#undef score_position
#undef display_scores
#undef tick
#undef init
