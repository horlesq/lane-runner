#pragma once
#include <SDL.h>
#include <string>
#include "platform/Window.hpp"

// The game window and its renderer. The game draws in 640x480; SDL scales that to the window.
class Display {
public:
    // Opens the window (with the icon, if the file exists). Returns false on failure (see SDL_GetError()).
    bool open(const char* title, const std::string& iconPath);

    SDL_Renderer* renderer() const { return renderer_.get(); }

    void toggleFullscreen();
    // Shows the finished frame (and, in a browser, waits for the next display refresh).
    void present();

private:
    WindowPtr window_;
    RendererPtr renderer_;   // destroyed before the window (members are destroyed in reverse order)
};
