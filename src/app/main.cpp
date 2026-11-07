#include <iostream>
#include <string>
#include "app/Game.hpp"
#include "audio/Audio.hpp"
#include "platform/Display.hpp"
#include "platform/Paths.hpp"
#include "platform/SdlContext.hpp"
#include "render/Renderer2D.hpp"

namespace {

    int fail(const char* what) {
        std::cerr << what << ": " << SDL_GetError() << '\n';
        return 1;
    }

    // Passes this frame's input to the game. Returns false when the window was closed.
    bool handleEvents(Game& game, Display& display) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                return false;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                game.onFocusLost();
            }
            if (event.type != SDL_KEYDOWN || event.key.repeat != 0) {
                continue;
            }
            const SDL_Keycode key = event.key.keysym.sym;
            const bool alt = (event.key.keysym.mod & KMOD_ALT) != 0;
            if (key == SDLK_F11 || (key == SDLK_RETURN && alt)) {   // fullscreen on/off, on any screen
                display.toggleFullscreen();
            } else {
                game.handleKey(key);
            }
        }
        return true;
    }

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    // Destroyed in reverse order: the game, the display, audio, then SDL itself.
    SdlContext sdl;
    if (!sdl.ok()) {
        return fail("SDL error");
    }
    Audio audio;
    if (!audio.ok()) {
        std::cerr << "No audio device, running silently: " << SDL_GetError() << '\n';
    }

    const std::string assetDir = findAssetDir();
    Display display;
    if (!display.open("Lane Runner", assetDir + "art/icon.png")) {
        return fail("SDL error");
    }
    Renderer2D gfx(display.renderer());
    if (!gfx.loadAssets(assetDir)) {
        return fail(("Asset error (looked in " + assetDir + ")").c_str());
    }

    Game game(audio, findSavePath());

    Uint32 lastTicks = SDL_GetTicks();
    while (handleEvents(game, display) && !game.quitRequested()) {
        const Uint32 now = SDL_GetTicks();
        game.update(static_cast<float>(now - lastTicks) / 1000.0f);   // seconds since the last frame
        lastTicks = now;

        game.draw(gfx);
        display.present();
    }
    return 0;
}
