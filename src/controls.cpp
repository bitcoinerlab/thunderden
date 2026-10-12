#include "controls.h"
#include "keys.h"
#include <algorithm>
#include <array>

namespace td {
std::string_view ArrowText(char32_t character)
{
    switch (character) {
    case U'←': return "←";
    case U'↑': return "↑";
    case U'→': return "→";
    case U'↓': return "↓";
    default: return {};
    }
}

std::vector<ControlGlyph> ControlGlyphs(std::string_view text)
{
    Require(std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c <= 126; }), "Invalid control text");
    static constexpr std::pair<std::string_view, std::u32string_view> keys[]{
        {"Up/Down", U"↑ ↓"}, {"Arrows", U"← ↑ → ↓"}, {"Left", U"←"}, {"Right", U"→"},
        {"Up", U"↑"}, {"Down", U"↓"}, {"Enter", U"Enter"}, {"Esc", U"Esc"},
        {"Tab", U"Tab"}, {"TAB", U"Tab"}, {"Backspace", U"Backspace"}, {"Delete", U"Delete"}, {"Supr", U"Supr"}};
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
    std::vector<ControlGlyph> result;
    for (size_t i = 0; i < text.size();) {
        size_t matched = 0;
        if (!i || !letter(text[i - 1])) for (const auto& [name, display] : keys) {
            if (text.substr(i, name.size()) != name || (i + name.size() < text.size() && letter(text[i + name.size()]))) continue;
            for (auto c : display) result.push_back({c, true});
            matched = name.size(); break;
        }
        if (matched) { i += matched; continue; }
        const bool shortcut = letter(text[i]) && (!i || text[i - 1] == ' ' || text[i - 1] == '/')
            && i + 1 < text.size() && (text[i + 1] == ':' || text[i + 1] == '/');
        if (i + 3 < text.size() && text[i] == '1' && text[i + 1] == '-' && text[i + 2] >= '1'
            && text[i + 2] <= '9' && text[i + 3] == ':') {
            for (unsigned j = 0; j < 3; ++j) result.push_back({char32_t(text[i++]), true});
        } else result.push_back({char32_t(text[i++]), shortcut});
    }
    return result;
}

std::vector<std::vector<ControlGlyph>> WrapControls(std::string_view text, size_t columns)
{
    Require(columns >= 20, "Display is too narrow");
    const auto glyphs = ControlGlyphs(text);
    std::vector<std::vector<ControlGlyph>> rows;
    if (glyphs.empty()) rows.emplace_back();
    for (size_t offset = 0; offset < glyphs.size();) {
        size_t count = std::min(columns, glyphs.size() - offset);
        if (offset + count < glyphs.size() && glyphs[offset + count].character != ' ') {
            for (size_t split = count; split > 0; --split) if (glyphs[offset + split - 1].character == ' ') { count = split - 1; break; }
            if (!count) count = std::min(columns, glyphs.size() - offset);
        }
        rows.emplace_back(glyphs.begin() + offset, glyphs.begin() + offset + count);
        offset += count;
        if (offset < glyphs.size() && glyphs[offset].character == ' ') ++offset;
    }
    return rows;
}
}
