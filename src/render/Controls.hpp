#pragma once

// Control hints shown on screen. The handheld build (CMake option HANDHELD) names the console's
// buttons; its launcher maps them to the same keys the PC version reads:
// A/Start = Enter, B/Select = Escape, Y = Tab, D-pad = arrows.
namespace controls {
#ifdef LANE_RUNNER_HANDHELD
inline constexpr const char* kMenu = "UP/DOWN: CHOOSE   A: SELECT   B: QUIT";
inline constexpr const char* kShop = "UP/DOWN: CHOOSE   A: BUY   B: BACK";
inline constexpr const char* kGarage = "LEFT/RIGHT: CHOOSE   A: BUY OR DRIVE   B: BACK";
inline constexpr const char* kGuide = "A: DEBUG MODE   B: BACK";
inline constexpr const char* kDriving = "Y: SENSORS  B: PAUSE";
inline constexpr const char* kRetry = "A: RETRY";
inline constexpr const char* kToMenu = "B: MENU";
inline constexpr const char* kPaused = "UP/DOWN: CHOOSE   A: SELECT   B: RESUME";
#else
inline constexpr const char* kMenu = "UP/DOWN: CHOOSE   ENTER: SELECT   ESC: QUIT";
inline constexpr const char* kShop = "UP/DOWN: CHOOSE   ENTER: BUY   ESC: BACK";
inline constexpr const char* kGarage = "LEFT/RIGHT: CHOOSE   ENTER: BUY OR DRIVE   ESC: BACK";
inline constexpr const char* kGuide = "ENTER: DEBUG MODE   ESC: BACK";
inline constexpr const char* kDriving = "TAB: SENSORS  ESC: PAUSE";
inline constexpr const char* kRetry = "ENTER: RETRY";
inline constexpr const char* kToMenu = "ESC: MENU";
inline constexpr const char* kPaused = "UP/DOWN: CHOOSE   ENTER: SELECT   ESC: RESUME";
#endif
} // namespace controls
