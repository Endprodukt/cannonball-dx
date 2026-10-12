package io.github.endprodukt.cannonballdx;

import org.libsdl.app.SDLActivity;

/**
 * The game itself. SDL owns the surface, input and lifecycle; the engine is
 * libmain.so, built from the repository's root CMakeLists.txt.
 */
public class GameActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
