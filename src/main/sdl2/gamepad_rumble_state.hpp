#pragma once

// Gamepad rumble is intentionally independent from wheel force feedback.
// Keep the enable state separate from the configured rumble strength so users
// can turn rumble off without losing their preferred intensity.
namespace gamepad_rumble
{
    inline bool enabled = true;
}
