#pragma once

// Keep the original OStats interface while adding preserved base entry points
// that the DX wrapper can extend without modifying the original implementation.
#define do_timers(...) do_timers_base(__VA_ARGS__); void do_timers(__VA_ARGS__)
#define init_next_level(...) init_next_level_base(__VA_ARGS__); void init_next_level(__VA_ARGS__)
#include "ostats_base.hpp"
#undef init_next_level
#undef do_timers
