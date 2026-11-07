#include "platform/Display.hpp"
#include <SDL_image.h>
#include "core/Config.hpp"
#include "platform/Web.hpp"
#include "render/Layout.hpp"

bool Display::open(const char* title, const std::string& iconPath) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   // smooth sprite scaling

    // In a browser the page scales the canvas, so the window keeps its size.
    const Uint32 flags = kIsWeb ? 0u : static_cast<Uint32>(SDL_WINDOW_RESIZABLE);
    window_.reset(SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   layout::kWindowWidth, layout::kWindowHeight, flags));
    if (!window_) {
        return false;
    }

    // Title-bar icon; the game still runs if the file is missing.
    if (SDL_Surface* icon = IMG_Load(iconPath.c_str())) {
        SDL_SetWindowIcon(window_.get(), icon);
        SDL_FreeSurface(icon);
    }

    renderer_.reset(SDL_CreateRenderer(window_.get(), -1, SDL_RENDERER_PRESENTVSYNC));
    if (!renderer_) {
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_.get(), cfg::kScreenWidth, cfg::kScreenHeight);
    return true;
}

void Display::toggleFullscreen() {
    const bool fullscreen = (SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(window_.get(), fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void Display::present() {
    SDL_RenderPresent(renderer_.get());
    webWaitForNextFrame();   // browser: yield until the next display refresh (no-op elsewhere)
}
