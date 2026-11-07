cmake_minimum_required(VERSION 3.16)   # oldest CMake version we support
project(LaneRunner VERSION 0.9.0 LANGUAGES CXX)   # project name and version; CXX = C++ language

# The browser build (Emscripten) brings its own SDL2 ports; everything else uses the installed SDL2.
if(NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)    # find the installed SDL2 library
    find_package(SDL2_image REQUIRED)
endif()

set(CMAKE_CXX_STANDARD 17)             # use C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)    # fail instead of silently using an older standard
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++17, not -std=gnu++17 (portable code)

add_executable(lane-runner             # a "target": the program we build
    src/app/Game.cpp
    src/app/main.cpp
    src/audio/Audio.cpp
    src/platform/Display.cpp
    src/platform/Paths.cpp
    src/render/BitmapFont.cpp
    src/render/Renderer2D.cpp
    src/render/Renderer2DScreens.cpp
)

# Treat every warning as an error, like embedded / safety-critical teams do.
target_compile_options(lane-runner PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(NOT EMSCRIPTEN)
    target_link_libraries(lane-runner PRIVATE SDL2::SDL2main SDL2::SDL2 SDL2_image::SDL2_image)   # linker step!
endif()

target_include_directories(lane-runner PRIVATE src)

# Where the game finds its sprites when run from the build folder (see platform/Paths.cpp).
target_compile_definitions(lane-runner PRIVATE ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/")

# Handheld build (R36S): on-screen hints name the console buttons instead of keyboard keys.
#   cmake -B build-r36s -DHANDHELD=ON ...
option(HANDHELD "Show console button names (A, B, Y) instead of keyboard keys" OFF)
if(HANDHELD)
    target_compile_definitions(lane-runner PRIVATE LANE_RUNNER_HANDHELD)
endif()

# --- Browser build (WebAssembly) ---
# Needs the Emscripten SDK; produces index.html/.js/.wasm/.data and a zip for itch.io:
#   emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-web --target web-zip
if(EMSCRIPTEN)
    target_sources(lane-runner PRIVATE src/platform/Web.cpp)
    set(WEB_SDL_FLAGS -sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sSDL2_IMAGE_FORMATS=png)
    target_compile_options(lane-runner PRIVATE ${WEB_SDL_FLAGS})
    target_link_options(lane-runner PRIVATE
        ${WEB_SDL_FLAGS}
        -sASYNCIFY                       # lets the normal main loop yield to the browser every frame
        -sASYNCIFY_STACK_SIZE=65536
        -sSTACK_SIZE=1MB
        -sALLOW_MEMORY_GROWTH
        -lidbfs.js                       # save file kept in the browser's IndexedDB
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
        # Same asset set as the desktop package, at /assets/ in the virtual file system.
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/art/icon.png@/assets/art/icon.png"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/racing-pack/PNG/Cars@/assets/racing-pack/PNG/Cars"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/racing-pack/PNG/Characters@/assets/racing-pack/PNG/Characters"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/racing-pack/PNG/Motorcycles@/assets/racing-pack/PNG/Motorcycles"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/racing-pack/PNG/Objects@/assets/racing-pack/PNG/Objects")
    set_target_properties(lane-runner PROPERTIES OUTPUT_NAME index SUFFIX ".html"
                          LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html")

    # itch.io wants a zip with index.html at its root.
    add_custom_target(web-zip
        COMMAND ${CMAKE_COMMAND} -E tar cf "LaneRunner-${PROJECT_VERSION}-web.zip" --format=zip
                index.html index.js index.wasm index.data
        DEPENDS lane-runner
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    return()   # packaging, art tool and unit tests below are desktop-only
endif()

# Windows: embed the app icon in the .exe so Explorer and the taskbar show it.
if(WIN32)
    enable_language(RC)
    set(APP_ICON_RC "${CMAKE_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_ICON_RC}" "1 ICON \"${CMAKE_SOURCE_DIR}/assets/art/icon.ico\"\n")
    target_sources(lane-runner PRIVATE "${APP_ICON_RC}")

    # Release builds are GUI apps (no console window); debug builds keep the console for error output.
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(lane-runner PROPERTIES WIN32_EXECUTABLE ON)
    endif()
endif()

# --- Packaging ---
# Install layout: the executable with assets/ next to it (see platform/Paths.cpp).
# The same layout is the R36S port folder (/roms/ports/lanerunner/).
#   cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-release && cmake --build build-release --target package
install(TARGETS lane-runner RUNTIME DESTINATION .)
install(FILES assets/art/icon.png DESTINATION assets/art)
install(DIRECTORY
        assets/racing-pack/PNG/Cars
        assets/racing-pack/PNG/Characters
        assets/racing-pack/PNG/Motorcycles
        assets/racing-pack/PNG/Objects
    DESTINATION assets/racing-pack/PNG)
install(FILES assets/racing-pack/License.txt DESTINATION assets/racing-pack)
install(FILES LICENSE DESTINATION .)

# Windows has no system SDL: copy every DLL the game needs from the toolchain next to it.
# (RUNTIME_DEPENDENCIES needs CMake 3.21; MSYS2 ships a newer one.)
if(WIN32)
    if(POLICY CMP0207)
        cmake_policy(SET CMP0207 NEW)   # CMake 4.x: normalize DLL paths before the exclude regexes
    endif()
    get_filename_component(TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(TARGETS lane-runner
        RUNTIME_DEPENDENCIES
            DIRECTORIES "${TOOLCHAIN_BIN}"
            PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
            POST_EXCLUDE_REGEXES ".*[Ss]ystem32/.*\\.dll"
        RUNTIME DESTINATION .)
endif()

if(WIN32)
    set(CPACK_GENERATOR ZIP)
    set(PACKAGE_PLATFORM windows)
else()
    set(CPACK_GENERATOR TGZ)
    set(PACKAGE_PLATFORM linux-${CMAKE_SYSTEM_PROCESSOR})
endif()
set(CPACK_PACKAGE_FILE_NAME "LaneRunner-${PROJECT_VERSION}-${PACKAGE_PLATFORM}")
include(CPack)

# Cover art and icon generator. Not part of the normal build: run it after changing the art.
#   cmake --build build --target artwork && ./build/artwork assets/ assets/art/
add_executable(artwork EXCLUDE_FROM_ALL tools/artwork.cpp src/render/BitmapFont.cpp)
target_include_directories(artwork PRIVATE src)
target_compile_options(artwork PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(artwork PRIVATE SDL2::SDL2main SDL2::SDL2 SDL2_image::SDL2_image)


# Unit tests for the logic layer: no SDL, runs on any machine.
enable_testing()
add_executable(unit-tests tests/test_main.cpp)
target_include_directories(unit-tests PRIVATE src)
target_compile_options(unit-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME unit-tests COMMAND unit-tests)
