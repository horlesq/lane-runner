#include "platform/Web.hpp"

// Only compiled for the browser build (see CMakeLists.txt).
#include <emscripten.h>

EM_ASYNC_JS(void, webMountSaveStorage, (), {
    FS.mkdir('/libsdl');
    FS.mount(IDBFS, {}, '/libsdl');
    await new Promise((resolve) => FS.syncfs(true, (err) => {
        if (err) console.error('Loading saves failed:', err);
        resolve();
    }));
});

EM_JS(void, webPersistSaves, (), {
    FS.syncfs(false, (err) => {
        if (err) console.error('Saving failed:', err);
    });
});

EM_ASYNC_JS(void, webWaitForNextFrame, (), {
    await new Promise((resolve) => requestAnimationFrame(resolve));
});
