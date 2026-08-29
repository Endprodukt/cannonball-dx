/***************************************************************************
    CannonBall DX process-wide Force Feedback ownership guard.

    Some Windows wheel drivers do not tolerate two processes opening the same
    haptic device and creating effects at the same time. A local named mutex
    lets the first CannonBall DX instance own FFB. Any second instance on the
    same Windows session falls back to no FFB before SDL opens the haptic device.

    This is intentionally machine-local. Two cabinets on separate PCs each get
    their own FFB owner, so LAN/online multiplayer can still use FFB on both.
***************************************************************************/

#pragma once

#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ffb_instance_guard
{
#ifdef _WIN32
    class ProcessLock
    {
    public:
        ~ProcessLock()
        {
            if (owned && mutex)
                ReleaseMutex(mutex);

            if (mutex)
                CloseHandle(mutex);
        }

        bool claim()
        {
            if (attempted)
                return owned;

            attempted = true;
            mutex = CreateMutexA(nullptr, FALSE, "Local\\CannonBallDX_FFB_Owner");

            if (!mutex)
            {
                std::cout << "SDL FFB: unable to create process ownership lock; FFB disabled" << std::endl;
                return false;
            }

            const DWORD wait_result = WaitForSingleObject(mutex, 0);
            if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_ABANDONED)
            {
                owned = true;
                return true;
            }

            CloseHandle(mutex);
            mutex = nullptr;

            std::cout
                << "SDL FFB: another CannonBall DX instance already owns FFB; "
                << "FFB disabled for this instance"
                << std::endl;
            return false;
        }

    private:
        HANDLE mutex = nullptr;
        bool attempted = false;
        bool owned = false;
    };

    inline ProcessLock process_lock;

    inline bool claim()
    {
        return process_lock.claim();
    }
#else
    inline bool claim()
    {
        return true;
    }
#endif
}
