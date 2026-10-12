#pragma once
#include <string_view>
#include <vector>

namespace td {
struct ControlGlyph { char32_t character; bool accent; };
// Only application-owned instructions use this formatter. Review data stays ASCII.
std::vector<ControlGlyph> ControlGlyphs(std::string_view text);
std::vector<std::vector<ControlGlyph>> WrapControls(std::string_view text, size_t columns);
std::string_view ArrowText(char32_t character);
}
