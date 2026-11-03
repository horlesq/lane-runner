#pragma once
#include <SDL.h>
#include <string_view>

// Built-in 5x7 pixel font: no font file needed, so it works the same on every target.
// Supports A-Z (lowercase is drawn as uppercase), 0-9 and  : . ! + - / % < > _ ,
namespace font {

    constexpr int kGlyphWidth = 5;
    constexpr int kGlyphHeight = 7;
    constexpr int kAdvance = kGlyphWidth + 1;   // one empty column between characters

    // Draws text with its top-left corner at (x, y). Each font pixel becomes a scale x scale square.
    void drawText(SDL_Renderer* r, std::string_view text, int x, int y, int scale, SDL_Color color);

    // Same, with a dark drop shadow for readability on busy backgrounds.
    void drawTextShadow(SDL_Renderer* r, std::string_view text, int x, int y, int scale, SDL_Color color);

    constexpr int textWidth(std::string_view text, int scale) {
        return text.empty() ? 0 : static_cast<int>(text.size()) * kAdvance * scale - scale;
    }

} // namespace font
