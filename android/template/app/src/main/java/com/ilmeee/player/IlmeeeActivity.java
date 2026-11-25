package com.ilmeee.player;

import org.libsdl.app.SDLActivity;

/**
 * Hosts IlmeeePlayer: SDLActivity loads libSDL3.so and libmain.so and calls
 * the player's main() (SDL_main), which extracts the game from the APK.
 */
public class IlmeeeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL3", "main" };
    }
}
