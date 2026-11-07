# Lane Runner

Top-down highway arcade game in C++17 and SDL2, built around a small driver-assistance stack:
simulated sensors produce object lists, a fusion step combines them, assist functions (ACC, AEB,
LKA, BSW) compute requests, and arbiters decide what reaches the vehicle. The logic runs in a fixed
20 ms cycle and is unit-tested without SDL.

![Cover](assets/art/cover.png)

## Video: how the assists work



https://github.com/user-attachments/assets/bd5728b8-443e-42dd-8394-d69b7c5b7d92



A 7-minute English walkthrough of the driver-assistance functions this game is built around: ACC, AEB,
LKA and BSW, the PID controller, sensors and fusion, the Kalman filter, arbitration and calibration.
Open the [release page](https://github.com/horlesq/lane-runner/releases/tag/adas-explainer) or
[download the video directly](https://github.com/horlesq/lane-runner/releases/download/adas-explainer/adas-explainer.mp4)
(1080p, 39 MB).

## Features

- **Assists:** adaptive cruise control (speed + gap PID, min-selection), automatic emergency braking
  (TTC stages with latching), lane keeping (lateral PID, rate-limited, driver override, degraded
  without lane markings), blind-spot warning with lane-change blocking, radar/camera fusion.
- **Sensors:** front radar, two rear radars and a front camera behind one `Sensor` interface. Radar
  ghosts under bridges, weak echoes from small objects, camera blinded by fog and tunnel glare,
  shorter camera range in rain.
- **Traffic:** IDM car following and MOBIL lane changes, turn signals, driver profiles (normal,
  aggressive, pickups, motorcycles), reaction times, synchronous updates from one snapshot.
- **Hazards:** roadworks with lane merges, accidents, oil, crosswind, fog, rain, tunnels, bridges.
  Layouts are randomised per run.
- **Game:** power-ups that switch assists on, near-miss combos, shields, coins, shop, garage with
  unlockable cars, persistent progress, crash cause shown on the game-over screen.

## Building

Requirements: CMake ≥ 3.16, a C++17 compiler, SDL2, SDL2_image.

### Windows (MSYS2 UCRT64)

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_image
cmake -B build -G Ninja
cmake --build build
./build/lane-runner.exe
```

### Linux

```sh
sudo apt install build-essential cmake ninja-build libsdl2-dev libsdl2-image-dev
cmake -B build -G Ninja
cmake --build build
./build/lane-runner
```

The game looks for `assets/` next to the executable first, so a packaged copy (executable plus
`assets/` folder) runs from anywhere. If none is found it falls back to `assets/` in the source tree
(`ASSET_DIR`, set at configure time), which is what a run from the build folder uses.

### Build options

| Option | Default | Effect |
|---|---|---|
| `HANDHELD` | `OFF` | On-screen hints name console buttons (A, B, Y) instead of keyboard keys |

```sh
cmake -B build-handheld -G Ninja -DHANDHELD=ON
```

All targets compile with `-Wall -Wextra -Wpedantic -Werror`.

### Packaging a release

```sh
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
cmake --build build-release --target package
```

This produces `LaneRunner-<version>-windows.zip` (or `-linux-<arch>.tar.gz`) in `build-release/`:
the executable, the `assets/` it needs, and on Windows every DLL it depends on, so it runs on a
PC without MSYS2. The Windows release build has no console window. Linux packages use the
system's SDL2 and SDL2_image.

### Browser (WebAssembly)

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html); its
SDL2 and SDL2_image ports are downloaded on the first build.

```sh
emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target web-zip
```

This produces `build-web/LaneRunner-<version>-web.zip` (`index.html` plus `.js`, `.wasm` and
`.data`), ready to upload as an HTML game. Progress is kept in the browser's IndexedDB. To try it
locally, serve the folder (`python -m http.server` in `build-web/`) and open `index.html`.

## Tests

```sh
cmake --build build --target unit-tests
ctest --test-dir build --output-on-failure
```

The `unit-tests` target has no SDL dependency. It covers the PID, ACC, AEB, LKA, BSW, arbiters,
sensors and fusion, traffic behaviour (no crashes, merging at roadworks over several seeds), hazards,
power-ups, progress save/load and the crash report.

## Controls

| Key | Menu | Driving |
|---|---|---|
| Up / Down | move selection | throttle / brake |
| Left / Right | change car (garage) | steer |
| Enter / Space | confirm | – |
| Esc | back / quit | pause (resume when paused) |
| Tab | – | sensor view on/off |
| A / L / B / F | – | toggle ACC / LKA / BSW / fusion while charged |
| F11 / Alt+Enter | fullscreen on/off | fullscreen on/off |

Assists switch on automatically when you drive through their power-up.

## Project layout

```
src/
  core/        Config constants, FixedVector (no-heap container), Span (non-owning view)
  world/       Vehicle, traffic (IDM/MOBIL driver model), hazards, obstacles, wind, oil, power-ups
  sensors/     Sensor interface, Radar, Camera
  perception/  Object list, lead selection, radar/camera fusion
  functions/   ACC, AEB, LKA, BSW state machines
  control/     PID template, longitudinal and steering arbiters
  game/        Progress and shop, near-miss tracking, crash report
  render/      SDL renderer, bitmap font, HUD/menu view models, control hints
  audio/       Synthesised sound effects and engine hum
  platform/    RAII wrappers for SDL init and window
  app/         main loop: input, fixed-step update, drawing
tests/         Unit tests (no SDL)
tools/         Cover and icon generator (`artwork` target, not built by default)
assets/        Sprites and generated art
```

Only `app/`, `render/`, `audio/` and `platform/` depend on SDL.

### Main loop

1. Poll input events (one-shot actions per screen).
2. Accumulate frame time and run the logic in fixed 20 ms steps:
   hazards → sensors → fusion → ACC / AEB / LKA / BSW → arbiters → ego vehicle → traffic → collisions.
3. Draw the frame from the current state.

No heap allocation happens in the cyclic part; containers have fixed capacity.

## Save data

Progress (best score, coins, upgrades, cars) is stored as `save.txt` in the SDL preference path, e.g.
`%APPDATA%\LaneRunner\LaneRunner\` on Windows and `~/.local/share/LaneRunner/LaneRunner/` on Linux.

## Credits

Code: MIT, see [LICENSE](LICENSE). Sprites: [Racing Pack](https://www.kenney.nl) by Kenney, CC0.
