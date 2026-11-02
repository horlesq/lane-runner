cmake_minimum_required(VERSION 3.16)   # oldest CMake version we support
project(LaneRunner CXX)                # project name; CXX = C++ language

find_package(SDL2 REQUIRED)    # find the installed SDL2 library
find_package(SDL2_image REQUIRED)

set(CMAKE_CXX_STANDARD 17)             # use C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)    # fail instead of silently using an older standard
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++17, not -std=gnu++17 (portable code)

add_executable(lane-runner             # a "target": the program we build
    src/app/main.cpp
    src/audio/Audio.cpp
    src/render/BitmapFont.cpp
    src/render/Renderer2D.cpp
)

# Treat every warning as an error, like embedded / safety-critical teams do.
target_compile_options(lane-runner PRIVATE -Wall -Wextra -Wpedantic -Werror)

target_link_libraries(lane-runner PRIVATE SDL2::SDL2main SDL2::SDL2 SDL2_image::SDL2_image)   # linker step!

target_include_directories(lane-runner PRIVATE src)

# Where the game finds its sprites when run from the build folder.
target_compile_definitions(lane-runner PRIVATE ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/")

# Windows: embed the app icon in the .exe so Explorer and the taskbar show it.
if(WIN32)
    enable_language(RC)
    set(APP_ICON_RC "${CMAKE_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_ICON_RC}" "1 ICON \"${CMAKE_SOURCE_DIR}/assets/art/icon.ico\"\n")
    target_sources(lane-runner PRIVATE "${APP_ICON_RC}")
endif()

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
