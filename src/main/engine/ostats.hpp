#pragma once

// Keep the original OStats interface while adding a preserved base entry point
// that the DX wrapper uses to accumulate Endless distance/time first.
#define do_timers(...) do_timers_base(__VA_ARGS__); void do_timers(__VA_ARGS__)
#include "ostats_base.hpp"
#undef do_timers
