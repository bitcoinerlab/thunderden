#include "seed_ocr.h"
#include "cbor.h"
#include "english.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <numeric>
#include <optional>

namespace td {
namespace {
std::string_view Text(std::span<const unsigned char> text)
{
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

SecretBytes Normalize(std::span<const unsigned char> raw)
{
    SecretBytes word(raw.begin(), raw.end());
    for (auto& c : word) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        else if (c == '0') c = 'o';
        else if (c == '1' || c == '|' || c == '/') c = 'l';
        else if (c == '5') c = 's';
        else if (c == '8') c = 'b';
        else if (c == '+') c = 't';
    }
    return word;
}

bool Known(std::span<const unsigned char> word)
{
    return std::binary_search(ENGLISH.begin(), ENGLISH.end(), Text(word));
}

unsigned Distance(std::string_view a, std::string_view b)
{
    if (a.size() > b.size() + 2 || b.size() > a.size() + 2) return 3;
    std::array<unsigned, 9> row{};
    std::iota(row.begin(), row.end(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        auto diagonal = row[0]; row[0] = i + 1;
        for (size_t j = 0; j < b.size(); ++j) {
            const auto old = row[j + 1];
            row[j + 1] = std::min({row[j] + 1, old + 1, diagonal + (a[i] != b[j])});
            diagonal = old;
        }
    }
    return row[b.size()];
}

std::vector<bool> NumberLabelFragments(const std::vector<OCRToken>& tokens)
{
    const auto same_row = [](const auto& a, const auto& b) {
        return std::abs(int(a.y + a.height / 2) - int(b.y + b.height / 2)) <= int(std::min(a.height, b.height) / 2);
    };
    const auto word_text = [](const OCRToken& token) {
        return (!ReadOCRNumber(token.text).length || token.prefix_width) && std::any_of(token.text.begin(), token.text.end(), [](auto c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        });
    };
    struct Label { unsigned x, y, width, height, number, word_x; };
    std::vector<Label> labels;
    for (const auto& token : tokens) {
        const auto number = ReadOCRNumber(token.text);
        if (token.prefix_width) {
            labels.push_back({token.x, token.y, token.prefix_width, token.height, number.value, token.x + token.word_offset});
            continue;
        }
        if (!number.value || number.length != token.text.size() || token.width > 2 * token.height) continue;
        const OCRToken* word = nullptr;
        for (const auto& other : tokens) {
            if (other.x <= token.x + token.width + token.height / 2 || other.x > token.x + token.width + 3 * token.height
                || !same_row(token, other) || !word_text(other) || other.prefix_width) continue;
            if (!word || other.x < word->x) word = &other;
        }
        if (!word) continue;
        // Split digits and punctuation must not supply multiple anchors on one row.
        const unsigned pad = token.height / 2;
        if (std::any_of(tokens.begin(), tokens.end(), [&](const auto& other) {
            return &other != &token && &other != word && same_row(token, other)
                && other.x < std::min(word->x, token.x + token.width + pad)
                && other.x + other.width + pad > token.x;
        })) continue;
        labels.push_back({token.x, token.y, token.width, token.height, number.value, word->x});
    }
    std::sort(labels.begin(), labels.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    std::vector<bool> ignored(tokens.size());
    for (size_t first = 0; first < labels.size();) {
        size_t end = first + 1;
        while (end < labels.size() && labels[end].x <= labels[first].x + labels[first].height / 2) ++end;
        const size_t begin = first; first = end;
        if (end - begin < 3) continue;
        std::sort(labels.begin() + begin, labels.begin() + end, [](const auto& a, const auto& b) { return a.y < b.y; });
        unsigned left = UINT32_MAX, right = 0, word_left = UINT32_MAX, word_right = 0, height = UINT32_MAX;
        bool consistent = true;
        for (size_t i = begin; i < end; ++i) {
            const auto& label = labels[i];
            if (i > begin && (label.number <= labels[i - 1].number || same_row(label, labels[i - 1]))) consistent = false;
            left = std::min(left, label.x); right = std::max(right, label.x + label.width);
            word_left = std::min(word_left, label.word_x); word_right = std::max(word_right, label.word_x);
            height = std::min(height, label.height);
        }
        if (!consistent || right >= word_left || word_right - word_left > height) continue;
        // A local label band, bounded by recognized labels. Attached prefixes
        // contribute only their character-aligned bounds, never the whole word box.
        const unsigned pad = height / 4;
        left -= std::min(left, pad); right = std::min(right + pad, word_left);
        const unsigned top = labels[begin].y + labels[begin].height / 2;
        const unsigned bottom = labels[end - 1].y + labels[end - 1].height / 2;
        for (const auto& word : tokens) {
            const unsigned center = word.y + word.height / 2;
            const unsigned x = word.x + word.word_offset;
            if (!word_text(word) || x < right || x + pad < word_left || x > word_right + pad
                || center < top || center > bottom) continue;
            std::vector<size_t> fragments;
            for (size_t i = 0; i < tokens.size(); ++i) {
                const auto& token = tokens[i];
                const auto number = ReadOCRNumber(token.text);
                if (token.x >= left && token.x + token.width <= right && same_row(token, word)
                    && !(number.length && number.length < token.text.size())) fragments.push_back(i);
            }
            // Preserve a single readable number for normal prefix association.
            // Otherwise discard the label group without guessing its numeric value.
            if (fragments.size() == 1 && ReadOCRNumber(tokens[fragments.front()].text).value) continue;
            for (auto i : fragments) ignored[i] = true;
        }
    }
    return ignored;
}
}

bool SeedWordCount(size_t count) { return count >= 12 && count <= MAX_SEED_WORDS && count % 3 == 0; }

OCRNumber ReadOCRNumber(std::span<const unsigned char> text)
{
    auto raw = Text(text);
    if (raw.empty()) return {};
    const char close = raw.front() == '(' ? ')' : raw.front() == '[' ? ']' : 0;
    const size_t start = close ? 1 : 0;
    if (start == raw.size() || raw[start] < '0' || raw[start] > '9') return {};
    unsigned number = 0;
    const auto [end, error] = std::from_chars(raw.data() + start, raw.data() + raw.size(), number);
    size_t length = end - raw.data();
    if (close) {
        if (length == raw.size() || raw[length] != close) return {};
        ++length;
    }
    if (length < raw.size() && OCR_NUMBER_SEPARATORS.find(raw[length]) == std::string_view::npos && !close) return {};
    while (length < raw.size() && OCR_NUMBER_SEPARATORS.find(raw[length]) != std::string_view::npos) ++length;
    return {error == std::errc{} && number >= 1 && number <= MAX_SEED_WORDS ? number : 0, length};
}

SeedDraft DecodeSeedWords(std::span<const uint8_t> packet)
{
    CborReader in(packet);
    const auto count = in.Array(MAX_OCR_TOKENS);
    std::vector<OCRToken> tokens;
    for (size_t i = 0; i < count; ++i) {
        in.Tuple(10);
        OCRToken token;
        token.x = in.UInt(1920); token.y = in.UInt(1080);
        token.width = in.UInt(1920); token.height = in.UInt(1080); token.line = in.UInt(MAX_OCR_TOKENS - 1);
        const auto text = in.Bytes(32); token.text.assign(text.begin(), text.end());
        const auto candidates = in.Array(MAX_WORD_SUGGESTIONS);
        for (size_t j = 0; j < candidates; ++j) {
            const auto word = in.Bytes(8); token.candidates.emplace_back(word.begin(), word.end());
        }
        token.confident = in.Bool();
        token.prefix_width = in.UInt(1920); token.word_offset = in.UInt(1920);
        tokens.push_back(std::move(token));
    }
    in.End();
    auto draft = ReadSeedTokens(tokens);
    SuggestSeedChecksum(draft);
    return draft;
}

std::vector<SecretBytes> WordSuggestions(std::span<const unsigned char> raw)
{
    Require(raw.size() <= 32, "OCR word is too long");
    auto word = Normalize(raw);
    if (Known(word)) return {std::move(word)};
    if (word.size() < 3 || word.size() > 10) return {};
    std::vector<SecretBytes> result;
    unsigned best = 3;
    size_t matches = 0;
    for (const auto candidate : ENGLISH) {
        const auto distance = Distance(Text(word), candidate);
        if (distance > 2 || distance > best) continue;
        if (distance < best) { result.clear(); matches = 0; best = distance; }
        ++matches;
        if (result.size() < 3) result.emplace_back(candidate.begin(), candidate.end());
    }
    // A large tie is not useful evidence. Never conceal omitted tied candidates.
    if (matches > 3) result.clear();
    return result;
}

SeedDraft ReadSeedTokens(const std::vector<OCRToken>& tokens)
{
    Require(tokens.size() <= MAX_OCR_TOKENS, "Too much text detected. Capture only your recovery words.");
    for (const auto& token : tokens) {
        Require(token.x <= 1920 && token.y <= 1080 && token.width > 0 && token.width <= 1920
            && token.height > 0 && token.height <= 1080 && token.line < MAX_OCR_TOKENS
            && token.text.size() <= 32 && token.candidates.size() <= MAX_WORD_SUGGESTIONS
            && (!token.confident || !token.candidates.empty()), "Invalid OCR result");
        for (auto c : token.text) Require(c >= 32 && c <= 126, "Invalid OCR text");
        const auto prefix = ReadOCRNumber(token.text);
        Require((!token.prefix_width && !token.word_offset) || (prefix.value && prefix.length < token.text.size()
            && token.prefix_width && token.prefix_width <= token.word_offset && token.word_offset < token.width), "Invalid OCR prefix geometry");
        for (size_t i = 0; i < token.candidates.size(); ++i) {
            Require(Known(token.candidates[i]), "Invalid OCR word candidate");
            Require(std::find(token.candidates.begin(), token.candidates.begin() + i, token.candidates[i]) == token.candidates.begin() + i,
                "Repeated OCR word candidate");
        }
    }
    SeedDraft draft;
    const auto label_fragments = NumberLabelFragments(tokens);
    std::vector<std::pair<unsigned, const OCRToken*>> prefixes;
    std::vector<unsigned> lines;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (label_fragments[i]) continue;
        const auto& token = tokens[i];
        auto raw = Text(token.text);
        const auto parsed = ReadOCRNumber(token.text);
        const unsigned number = parsed.value;
        if (parsed.length) {
            draft.number_warning |= !number;
            raw.remove_prefix(parsed.length);
            if (raw.empty()) {
                if (number) prefixes.emplace_back(number, &token);
                continue;
            }
        }
        if (raw.size() == 2 && std::string_view(".)-:/").find(raw.back()) != std::string_view::npos) {
            draft.number_warning = true; // e.g. a printed "9." read as "g."; never a BIP39 word.
            continue;
        }
        // Standalone bullets/rules are decoration. A number with no recognized
        // word still produces its own missing slot during prefix association.
        if (!raw.empty() && raw.find_first_not_of("-*_") == std::string_view::npos) continue;
        if (!raw.empty() && std::all_of(raw.begin(), raw.end(), [](char c) { return OCR_NUMBER_SEPARATORS.find(c) != std::string_view::npos || c == '(' || c == '['; })) {
            // A recognizer can emit "10", "-", "word" separately. The separator
            // is not another seed slot; an unpaired number still retains a blank.
            if (std::any_of(tokens.begin(), tokens.end(), [&](const auto& other) {
                if (!ReadOCRNumber(other.text).length) return false;
                const auto dy = std::abs(int(token.y + token.height / 2) - int(other.y + other.height / 2));
                return other.line == token.line || (token.x >= other.x && token.x <= other.x + other.width + 2 * token.height
                    && dy <= int((std::max(token.height, other.height) + 1) / 2));
            })) continue;
            raw = {};
        }
        SecretBytes original(raw.begin(), raw.end());
        auto normalized = Normalize(original);
        const unsigned x = token.x + token.word_offset;
        unsigned width = token.width - token.word_offset;
        // Join split lettering only within one OCR line, never two valid words.
        bool joined_word = false;
        if (!normalized.empty() && !Known(normalized) && i + 1 < tokens.size() && !label_fragments[i + 1] && tokens[i + 1].line == token.line) {
            const auto next = Normalize(tokens[i + 1].text);
            if (!Known(next)) {
                auto joined = normalized; joined.insert(joined.end(), next.begin(), next.end());
                if (Known(joined)) {
                    original.push_back(' '); original.insert(original.end(), tokens[i + 1].text.begin(), tokens[i + 1].text.end());
                    normalized = std::move(joined);
                    joined_word = true;
                    width = tokens[i + 1].x + tokens[i + 1].width > x ? tokens[i + 1].x + tokens[i + 1].width - x : width;
                    ++i;
                }
            }
        }
        auto candidates = !joined_word && !token.candidates.empty() ? token.candidates : WordSuggestions(normalized);
        SecretBytes value;
        const bool ranked = !joined_word && !token.candidates.empty();
        const bool uncertain = ranked ? !token.confident : candidates.size() != 1;
        // Confidence marks a proposal for review; it must not hide an available
        // best candidate behind an empty slot.
        if (!candidates.empty()) value = candidates.front();
        draft.words.push_back({std::move(original), std::move(value), std::move(candidates),
            x, token.y, width, token.height, number, uncertain});
        lines.push_back(token.line);
    }
    // Detected regions from neighboring rows/columns can interleave. Associate
    // standalone numbers after extraction, rather than losing a pending number
    // whenever an unrelated word is emitted first.
    for (const auto& [number, prefix] : prefixes) {
        size_t nearest = draft.words.size();
        unsigned distance = UINT32_MAX;
        for (size_t i = 0; i < draft.words.size(); ++i) {
            const auto& word = draft.words[i];
            if (word.number || word.x < prefix->x || word.x > prefix->x + prefix->width + 3 * word.height) continue;
            const auto dy = unsigned(std::abs(int(word.y + word.height / 2) - int(prefix->y + prefix->height / 2)));
            if (lines[i] != prefix->line && dy > (std::max(word.height, prefix->height) + 1) / 2) continue;
            const auto gap = unsigned(std::abs(int(word.x) - int(prefix->x + prefix->width))) + dy;
            if (gap < distance) { nearest = i; distance = gap; }
        }
        if (nearest < draft.words.size()) draft.words[nearest].number = number;
        else {
            SeedWord word;
            word.x = prefix->x; word.y = prefix->y; word.width = prefix->width; word.height = prefix->height;
            word.number = number; word.uncertain = true;
            draft.words.push_back(std::move(word)); lines.push_back(prefix->line);
        }
    }
    Require(draft.words.size() <= MAX_DRAFT_WORDS, "More than 30 word slots detected. Capture closer or enter manually.");
    Require(!draft.words.empty(), "No recovery words detected. Try another capture or enter manually.");
    OrderSeedWords(draft);
    draft.numbered = !draft.number_warning;
    bool any_number = false;
    unsigned previous = 0;
    for (const auto& word : draft.words) {
        any_number |= word.number != 0;
        if (!word.number || word.number <= previous) draft.numbered = false;
        if (word.number) previous = word.number;
    }
    // Numbering consistent with the selected order can expose gaps, but does
    // not invent trailing entries or change the selected traversal.
    if (draft.numbered && draft.words.size() >= 2) {
        std::vector<SeedWord> ordered;
        unsigned next = 1;
        for (auto& word : draft.words) {
            while (next < word.number) {
                SeedWord missing; missing.number = next++; missing.uncertain = true;
                ordered.push_back(std::move(missing));
            }
            ++next; ordered.push_back(std::move(word));
        }
        draft.words = std::move(ordered);
    }
    else if (previous > draft.words.size()) draft.numbered = false;
    draft.number_warning |= any_number && !draft.numbered;
    return draft;
}

void OrderSeedWords(SeedDraft& draft)
{
    auto& words = draft.words;
    if (words.empty()) return;
    std::vector<unsigned> heights;
    for (const auto& word : words) heights.push_back(word.height);
    std::sort(heights.begin(), heights.end());
    const auto gutter = std::max(4U, heights[heights.size() / 2]);
    const auto by_rows = [&](auto begin, auto end) {
        std::stable_sort(begin, end, [](const auto& a, const auto& b) { return a.y + a.height / 2 < b.y + b.height / 2; });
        unsigned rows = 0;
        for (auto row = begin; row != end;) {
            auto next = row + 1;
            const auto center = row->y + row->height / 2;
            while (next != end && next->y + next->height / 2 <= center + gutter / 2) ++next;
            std::stable_sort(row, next, [](const auto& a, const auto& b) { return a.x < b.x; });
            row = next; ++rows;
        }
        return rows;
    };
    auto row_order = words;
    const auto rows = by_rows(row_order.begin(), row_order.end());
    // A column boundary needs actual horizontal whitespace between text spans.
    // Left-edge jitter or a short word cannot split one vertical list in two.
    std::stable_sort(words.begin(), words.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    size_t first = 0;
    while (first < words.size()) {
        size_t end = first + 1;
        unsigned right = words[first].x + words[first].width;
        while (end < words.size() && words[end].x <= right + gutter) {
            right = std::max(right, words[end].x + words[end].width); ++end;
        }
        by_rows(words.begin() + first, words.begin() + end);
        first = end;
    }
    const auto increasing = [](const auto& order) {
        unsigned previous = 0, clues = 0;
        for (const auto& word : order) if (word.number) {
            if (word.number <= previous) return false;
            previous = word.number; ++clues;
        }
        return clues >= 4;
    };
    const bool row_numbers = !draft.number_warning && increasing(row_order);
    const bool column_numbers = !draft.number_warning && increasing(words);
    // Choose once on capture. Labels select only between these two geometric
    // traversals; editing and checksum assistance never run this decision again.
    if (row_numbers != column_numbers ? row_numbers : rows <= 2) words = std::move(row_order);
}

SecretBytes DraftMnemonic(const SeedDraft& draft)
{
    Require(SeedWordCount(draft.words.size()), "A BIP39 phrase must contain 12, 15, 18, 21 or 24 words.");
    SecretBytes result;
    for (const auto& word : draft.words) {
        Require(!word.value.empty() && word.value.size() <= 8, "Correct the unreadable words first.");
        if (!result.empty()) result.push_back(' ');
        result.insert(result.end(), word.value.begin(), word.value.end());
    }
    return result;
}

bool DraftChecksum(const SeedDraft& draft)
{
    try { ValidateMnemonic(DraftMnemonic(draft)); return true; }
    catch (const std::invalid_argument&) { return false; }
}

bool SuggestSeedChecksum(SeedDraft& draft)
{
    if (!SeedWordCount(draft.words.size())) return false;
    size_t combinations = 1;
    for (const auto& word : draft.words) {
        if (word.value.empty() || (word.uncertain && word.alternatives.empty())) return false;
        combinations *= word.uncertain ? word.alternatives.size() : 1;
        if (combinations > 64) return false;
    }
    std::optional<SeedDraft> match;
    SecretBytes sentence;
    auto candidate = draft;
    for (size_t choice = 0; choice < combinations; ++choice) {
        size_t remaining = choice;
        for (auto& word : candidate.words) {
            if (!word.uncertain) continue;
            word.value = word.alternatives[remaining % word.alternatives.size()];
            remaining /= word.alternatives.size();
        }
        if (!DraftChecksum(candidate)) continue;
        const auto current = DraftMnemonic(candidate);
        if (match && sentence != current) return false;
        sentence = current;
        match = candidate;
    }
    if (!match) return false;
    draft = std::move(*match);
    draft.checksum_suggested = true;
    return true;
}
}
