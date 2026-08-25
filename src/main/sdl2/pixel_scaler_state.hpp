#pragma once

#include <atomic>
#include <iostream>

#include "frontend/config.hpp"

namespace pixel_scaler
{
    // Keep the original numeric values so existing config.xml files remain
    // compatible. The two former 2x slots are legacy-only and are normalized
    // to their corresponding 3x modes; they are never exposed or cycled to.
    enum Mode
    {
        OFF = 0,
        LEGACY_XBRZ_2X = 1,
        XBRZ_3X = 2,
        XBRZ_4X = 3,
        XBRZ_5X = 4,
        XBRZ_6X = 5,
        LEGACY_HQX_2X = 6,
        HQX_3X = 7,
        HQX_4X = 8,
        MODE_COUNT = 9,
    };

    inline std::atomic<int> mode{OFF};
    inline std::atomic<int> last_mode{XBRZ_4X};
    inline std::atomic<bool> renderer_restart_requested{false};

    inline bool valid(int value)
    {
        // Legacy 2x values are still considered readable config values. They
        // are normalized immediately by set()/normalize().
        return value >= OFF && value < MODE_COUNT;
    }

    inline int normalize(int value)
    {
        switch (value)
        {
            case LEGACY_XBRZ_2X: return XBRZ_3X;
            case LEGACY_HQX_2X:  return HQX_3X;
            case OFF:
            case XBRZ_3X:
            case XBRZ_4X:
            case XBRZ_5X:
            case XBRZ_6X:
            case HQX_3X:
            case HQX_4X:
                return value;
            default:
                return OFF;
        }
    }

    inline bool active(int value)
    {
        return normalize(value) != OFF;
    }

    inline bool is_xbrz(int value)
    {
        value = normalize(value);
        return value >= XBRZ_3X && value <= XBRZ_6X;
    }

    inline bool is_hqx(int value)
    {
        value = normalize(value);
        return value >= HQX_3X && value <= HQX_4X;
    }

    inline int factor(int value)
    {
        switch (normalize(value))
        {
            case XBRZ_3X:
            case HQX_3X:
                return 3;
            case XBRZ_4X:
            case HQX_4X:
                return 4;
            case XBRZ_5X:
                return 5;
            case XBRZ_6X:
                return 6;
            default:
                return 1;
        }
    }

    inline const char* name(int value)
    {
        switch (normalize(value))
        {
            case XBRZ_3X: return "XBRZ 3X";
            case XBRZ_4X: return "XBRZ 4X";
            case XBRZ_5X: return "XBRZ 5X";
            case XBRZ_6X: return "XBRZ 6X";
            case HQX_3X:  return "HQX 3X";
            case HQX_4X:  return "HQX 4X";
            default:      return "OFF";
        }
    }

    inline void set(int value)
    {
        value = normalize(value);

        // Also migrate a legacy persisted "last scaler" even when the current
        // mode is OFF, so the next save no longer writes a removed 2x mode.
        int normalized_last = normalize(
            last_mode.load(std::memory_order_relaxed));
        if (!active(normalized_last))
            normalized_last = XBRZ_4X;
        last_mode.store(normalized_last, std::memory_order_relaxed);

        mode.store(value, std::memory_order_relaxed);
        if (active(value))
            last_mode.store(value, std::memory_order_relaxed);
    }

    // Every scaler transition is now handled inside the already-live
    // PixelScalerRenderer. In particular OFF <-> ON must not destroy or
    // recreate the SDL window, GLES context or shader program. Those context
    // restarts proved inherently fragile while the threaded video pipeline was
    // active. The renderer consumes this request at the normal frame boundary
    // and only changes CPU scaler buffers / game texture storage in place.
    inline void request_transition_restart(int previous, int next)
    {
        (void) previous;
        (void) next;
        renderer_restart_requested.store(true, std::memory_order_release);
    }

    // Full menu cycle. 3x stays available for users who prefer the lower-cost
    // mode or its look, even though the quick F6 cycle focuses on the stronger
    // 4x+ modes.
    inline int cycle()
    {
        const int previous = normalize(
            mode.load(std::memory_order_relaxed));
        int next = OFF;

        switch (previous)
        {
            case OFF:      next = XBRZ_3X; break;
            case XBRZ_3X: next = XBRZ_4X; break;
            case XBRZ_4X: next = XBRZ_5X; break;
            case XBRZ_5X: next = XBRZ_6X; break;
            case XBRZ_6X: next = HQX_3X;  break;
            case HQX_3X:  next = HQX_4X;  break;
            case HQX_4X:  next = OFF;     break;
            default:      next = OFF;     break;
        }

        set(next);
        request_transition_restart(previous, next);
        return next;
    }

    // Quick-cycle used by F6. Skip the more compromise-heavy 3x modes while
    // leaving them accessible from Enhancements.
    inline int cycle_hotkey()
    {
        const int previous = normalize(
            mode.load(std::memory_order_relaxed));
        int next = OFF;

        switch (previous)
        {
            case OFF:      next = XBRZ_4X; break;
            case XBRZ_3X:  next = XBRZ_4X; break;
            case XBRZ_4X:  next = XBRZ_5X; break;
            case XBRZ_5X:  next = XBRZ_6X; break;
            case XBRZ_6X:  next = HQX_4X;  break;
            case HQX_3X:   next = HQX_4X;  break;
            case HQX_4X:   next = OFF;     break;
            default:       next = OFF;     break;
        }

        set(next);
        request_transition_restart(previous, next);

        // F6 changes the scaler outside the frontend's normal save flow.
        // Persist it immediately because CannonBall-SE exits via _Exit() and
        // therefore has no guaranteed final config save on shutdown.
        if (!config.save())
            std::cerr << "Unable to save pixel scaler setting." << std::endl;

        return next;
    }

    inline bool consume_renderer_restart_request()
    {
        return renderer_restart_requested.exchange(false, std::memory_order_acq_rel);
    }
}
