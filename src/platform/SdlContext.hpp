#pragma once
#include <SDL.h>

// SDL_Init on construction, SDL_Quit on destruction. Create it first, so it is destroyed last.
class SdlContext {
public:
    SdlContext() : ok_(SDL_Init(SDL_INIT_VIDEO) == 0) {}

    ~SdlContext() {
        if (ok_) {
            SDL_Quit();
        }
    }

    SdlContext(const SdlContext&) = delete;
    SdlContext& operator=(const SdlContext&) = delete;

    bool ok() const { return ok_; }

private:
    bool ok_;
};
