#pragma once
#include <SDL.h>
// #include <iostream>

class SdlContext {
public:
    SdlContext() : ok_(SDL_Init(SDL_INIT_VIDEO) == 0) {}

    ~SdlContext() {
        // std::cout << "SDL closed\n";
        if (ok_) { SDL_Quit(); }
    }

    SdlContext(const SdlContext&) = delete;
    SdlContext& operator=(const SdlContext&) = delete;

    bool ok() const {
        return ok_;
    }

private:
    bool ok_;
};
