#pragma once
#include <string>

// Folder with the game's sprites, ending in '/'. A packaged game ships assets/ next to the
// executable; otherwise the source tree's assets/ is used (running from the build folder).
std::string findAssetDir();

// The save file, in the user's settings folder (in a browser: IndexedDB, loaded by this call).
std::string findSavePath();
