#pragma once

#include <SDL.h>

#include <algorithm>
#include <string>
#include <vector>

namespace display_utils
{
    inline int count()
    {
        const int displays = SDL_GetNumVideoDisplays();
        return displays > 0 ? displays : 0;
    }

    inline std::string name(int index)
    {
        const int displays = count();
        if (index < 0 || index >= displays)
            return {};

        const char* value = SDL_GetDisplayName(index);
        return value ? std::string(value) : std::string();
    }

    // Display indices are not stable across reconnects. Prefer a unique saved
    // name; for duplicate names use the saved index to disambiguate; then fall
    // back to any still-valid saved index and finally display 0.
    inline int resolve_preferred(int stored_index, const std::string& stored_name)
    {
        const int displays = count();
        if (displays <= 0)
            return 0;

        if (!stored_name.empty())
        {
            std::vector<int> matches;
            for (int i = 0; i < displays; ++i)
            {
                if (name(i) == stored_name)
                    matches.push_back(i);
            }

            if (matches.size() == 1)
                return matches.front();

            if (matches.size() > 1 &&
                std::find(matches.begin(), matches.end(), stored_index) != matches.end())
            {
                return stored_index;
            }
        }

        if (stored_index >= 0 && stored_index < displays)
            return stored_index;

        return 0;
    }
}
