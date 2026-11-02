cmake_minimum_required(VERSION 3.16)   # oldest CMake version we support
project(LaneRunner CXX)                # project name; CXX = C++ language

find_package(SDL2 REQUIRED)    # find the installed SDL2 library

set(CMAKE_CXX_STANDARD 17)             # use C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)    # fail instead of silently using an older standard
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++17, not -std=gnu++17 (portable code)

add_executable(lane-runner src/app/main.cpp)   # a "target": the program we build

# Treat every warning as an error, like embedded / safety-critical teams do.
target_compile_options(lane-runner PRIVATE -Wall -Wextra -Wpedantic -Werror)

target_link_libraries(lane-runner PRIVATE SDL2::SDL2main SDL2::SDL2)   # linker step!

target_include_directories(lane-runner PRIVATE src)