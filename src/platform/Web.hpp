#pragma once

// Browser build (Emscripten) hooks. On every other platform the functions are empty and compile away,
// so desktop and handheld builds behave exactly as before.
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __EMSCRIPTEN__
inline constexpr bool kIsWeb = true;

// SDL keeps its preference path under /libsdl, which lives in memory in a browser. Back it with
// IndexedDB and load what's stored there before the save file is read.
EM_ASYNC_JS(void, webMountSaveStorage, (), {
    FS.mkdir('/libsdl');
    FS.mount(IDBFS, {}, '/libsdl');
    await new Promise((resolve) => FS.syncfs(true, (err) => {
        if (err) console.error('Loading saves failed:', err);
        resolve();
    }));
});

// Copy the in-memory files back to IndexedDB, after every save.
EM_JS(void, webPersistSaves, (), {
    FS.syncfs(false, (err) => {
        if (err) console.error('Saving failed:', err);
    });
});

// Hand control back to the browser until the next display refresh.
EM_ASYNC_JS(void, webWaitForNextFrame, (), {
    await new Promise((resolve) => requestAnimationFrame(resolve));
});
#else
inline constexpr bool kIsWeb = false;

inline void webMountSaveStorage() {}
inline void webPersistSaves() {}
inline void webWaitForNextFrame() {}
#endif
