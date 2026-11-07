#include "platform/Paths.hpp"
#include <SDL.h>
#include "platform/Web.hpp"

std::string findAssetDir() {
    std::string dir = ASSET_DIR;   // injected by CMake: the source tree's assets/
    if (char* base = SDL_GetBasePath()) {
        const std::string packaged = std::string(base) + "assets/";
        SDL_free(base);
        if (SDL_RWops* probe = SDL_RWFromFile((packaged + "art/icon.png").c_str(), "rb")) {
            SDL_RWclose(probe);
            dir = packaged;
        }
    }
    return dir;
}

std::string findSavePath() {
    webMountSaveStorage();   // browser: load the stored save first (no-op elsewhere)
    std::string path = "lanerunner-save.txt";
    if (char* prefDir = SDL_GetPrefPath("LaneRunner", "LaneRunner")) {
        path = std::string(prefDir) + "save.txt";
        SDL_free(prefDir);
    }
    return path;
}
