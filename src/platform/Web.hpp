#pragma once

// Browser build (Emscripten) hooks, defined in Web.cpp. On every other platform the functions are
// empty and compile away, so desktop and handheld builds behave exactly as before.
#ifdef __EMSCRIPTEN__
inline constexpr bool kIsWeb = true;

extern "C" {
// SDL keeps its preference path under /libsdl, which lives in memory in a browser. Backs it with
// IndexedDB and loads what's stored there; call before the save file is read.
void webMountSaveStorage();
// Copies the in-memory files back to IndexedDB; call after every save.
void webPersistSaves();
// Hands control back to the browser until the next display refresh.
void webWaitForNextFrame();
}
#else
inline constexpr bool kIsWeb = false;

inline void webMountSaveStorage() {}
inline void webPersistSaves() {}
inline void webWaitForNextFrame() {}
#endif
