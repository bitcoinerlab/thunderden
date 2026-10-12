#include "seed_ocr.h"
#include "cbor.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <tuple>

namespace {
auto Bytes(std::string_view text) { return std::span(reinterpret_cast<const unsigned char*>(text.data()), text.size()); }
td::OCRToken Token(std::string_view text, unsigned x = 10, unsigned y = 10, unsigned line = 0)
{
    return {x, y, 90, 24, line, td::SecretBytes(text.begin(), text.end())};
}
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
void Reject(const std::function<void()>& action)
{
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid seed OCR data accepted");
}
std::string Text(const td::SecretBytes& bytes) { return {bytes.begin(), bytes.end()}; }
}

int main()
{
    try {
        for (size_t n = 0; n <= 100; ++n)
            Check(td::SeedWordCount(n) == (n == 12 || n == 15 || n == 18 || n == 21 || n == 24), "Wrong seed count limits");
        for (const auto prefix : {"1:", "01 ", "(1)", "[01]", "1]", "1;", "1___", "1.-"}) {
            const auto text = std::string(prefix) + "carbon";
            const auto number = td::ReadOCRNumber(Bytes(text));
            Check(number.value == 1 && text.substr(number.length) == "carbon", "Common number decoration was not parsed");
            const auto words = td::ReadSeedTokens({Token(text)});
            Check(words.words.size() == 1 && words.words[0].number == 1 && Text(words.words[0].value) == "carbon", "Number decoration became seed text");
        }
        for (const auto text : {"1ion", "0ffice", "(1carbon", "[1)carbon", "carbon", ""})
            Check(!td::ReadOCRNumber(Bytes(text)).length, "Ambiguous word text was treated as a numeric prefix");
        const auto boxed_number = td::ReadSeedTokens({Token("["), Token("1", 20), Token("]", 40), Token("carbon", 120)});
        Check(boxed_number.words.size() == 1 && boxed_number.numbered, "Split number brackets became extra slots");
        const auto separate_dot = td::ReadSeedTokens({Token("1", 10, 10, 0), Token(".", 105, 10, 1), Token("carbon", 140, 10, 2)});
        Check(separate_dot.words.size() == 1 && separate_dot.numbered, "A separately detected number dot stole the word's number");
        for (const auto marker : {"*", "-", "____"}) {
            const auto decorated = td::ReadSeedTokens({Token(marker), Token("carbon", 120)});
            Check(decorated.words.size() == 1 && Text(decorated.words[0].value) == "carbon", "An unnumbered bullet/rule became a seed word");
        }
        auto suggestions = td::WordSuggestions(Bytes("BAD9E"));
        Check(suggestions.size() == 1 && Text(suggestions[0]) == "badge", "Closest edit candidate was not preferred");
        suggestions = td::WordSuggestions(Bytes("ASENT"));
        Check(suggestions.size() == 2 && Text(suggestions[0]) == "absent" && Text(suggestions[1]) == "agent", "Ambiguous correction concealed");
        for (const auto& [raw, expected] : {std::pair{"a|so", "also"}, {"z00", "zoo"}, {"a+0m", "atom"}}) {
            suggestions = td::WordSuggestions(Bytes(raw));
            Check(suggestions.size() == 1 && Text(suggestions[0]) == expected, "OCR confusion was not normalized");
        }
        Check(td::WordSuggestions(Bytes("v")).empty(), "Numbering fragment invented a word");
        Check(td::WordSuggestions(Bytes("a66")).empty(), "Large candidate tie was silently truncated");
        Reject([] { td::WordSuggestions(Bytes(std::string(33, 'a'))); });
        auto joined = td::ReadSeedTokens({Token("a"), Token("dvice", 30)});
        Check(joined.words.size() == 1 && Text(joined.words[0].value) == "advice", "Split word was not joined");
        Check(td::ReadSeedTokens({Token("car"), Token("pet", 110)}).words.size() == 2, "Valid words were incorrectly merged");
        const auto baseline = td::ReadSeedTokens({Token("car", 10, 11), Token("pet", 60, 10)});
        Check(Text(baseline.words[0].value) == "car" && Text(baseline.words[1].value) == "pet", "Baseline jitter reversed adjacent words");
        Check(td::ReadSeedTokens({Token("a"), Token("dvice", 30, 10, 1)}).words.size() == 2, "Words merged across OCR boxes");
        const std::vector<std::string> phrase{"legal", "winner", "thank", "year", "wave", "sausage", "worth", "useful", "legal", "winner", "thank", "yellow"};
        std::string expected;
        for (const auto& word : phrase) { if (!expected.empty()) expected += ' '; expected += word; }
        td::ValidateMnemonic(Bytes(expected));
        for (bool partial_numbers : {false, true}) {
            std::vector<td::OCRToken> indented;
            for (unsigned i = 0; i < 12; ++i) {
                const auto prefix = partial_numbers && i != 8 && i != 9 ? std::to_string(i + 1) + "." : "";
                auto token = Token(prefix + phrase[i], i >= 10 ? 95 : i >= 8 ? 155 : 100, 50 + i * 50, i);
                token.width = i >= 10 ? 70 : 100;
                indented.push_back(std::move(token));
            }
            auto ordered = td::ReadSeedTokens(indented);
            Check(Text(td::DraftMnemonic(ordered)) == expected, "Indentation changed 9-10-11-12 into 11-12-9-10");
            std::swap(ordered.words[8], ordered.words[10]); std::swap(ordered.words[9], ordered.words[11]);
            const auto before = td::DraftMnemonic(ordered);
            td::SuggestSeedChecksum(ordered);
            Check(td::DraftMnemonic(ordered) == before, "Checksum assistance changed the established order");
        }
        for (bool numbered : {false, true}) {
            std::vector<td::OCRToken> tokens;
            for (unsigned row = 0; row < 6; ++row) for (unsigned col = 0; col < 2; ++col) {
                const auto i = row + col * 6;
                if (numbered) tokens.push_back(Token(std::to_string(i + 1) + ".", col * 400, row * 50, i * 2));
                tokens.push_back(Token(phrase[i], 30 + col * 400, row * 50, i * 2 + 1));
            }
            auto draft = td::ReadSeedTokens(tokens);
            Check(draft.numbered == numbered && !draft.number_warning, "Number prefixes were not associated with words");
            Check(draft.words.size() == 12 && Text(td::DraftMnemonic(draft)) == expected && td::DraftChecksum(draft), "Columns or duplicate words were lost");
            Check(td::SuggestSeedChecksum(draft), "Unique checksum proposal was not found");
        }
        const auto numbered_card = [&](unsigned columns, unsigned damaged, unsigned pieces) {
            std::vector<td::OCRToken> card;
            for (unsigned row = 0; row < 12; ++row) for (unsigned col = 0; col < columns; ++col) {
                const unsigned n = col * 12 + row + 1, x = 40 + col * 400, y = 50 + row * 40;
                for (unsigned piece = 0; piece < (row == damaged ? pieces : 1); ++piece) {
                    const auto text = row != damaged ? std::to_string(n) + ".-" : pieces == 1 ? "art"
                        : piece == 0 ? std::to_string(n / 10) : piece == 1 ? std::to_string(n % 10) + (pieces == 2 ? ".-" : "") : ".-";
                    auto label = Token(text, x + piece * 8, y, col * 24 + row * 2);
                    label.width = row == damaged && pieces > 1 ? 8 : 24;
                    if (text == "art") { label.candidates = {td::SecretBytes{'a','r','t'}}; label.confident = true; }
                    card.push_back(std::move(label));
                }
                auto word = Token(row == 5 ? "art" : phrase[row], x + 60, y + col, col * 24 + row * 2 + 1);
                if (row == 5) word.width = 20;
                card.push_back(std::move(word));
            }
            return card;
        };
        for (unsigned columns : {1, 2}) for (unsigned pieces : {1, 2, 3}) {
            const auto input = numbered_card(columns, 10, pieces);
            for (bool reversed : {false, true}) {
                auto card = input;
                if (reversed) std::reverse(card.begin(), card.end());
                const auto result = td::ReadSeedTokens(card);
                Check(result.words.size() == columns * 12, "Misread or fragmented labels created extra word slots");
                for (unsigned i = 0; i < result.words.size(); ++i) {
                    Check(Text(result.words[i].value) == (i % 12 == 5 ? "art" : phrase[i % 12]),
                        "Label filtering removed a genuine word or changed column order");
                    Check(result.words[i].number == (i % 12 == 10 ? 0 : i + 1), "Label filtering guessed a number or detached a readable label");
                }
            }
        }
        // Twenty-four numbered entries in row order; the second column's 10-
        // is a separate, confident TOP proposal. All other labels may be attached.
        for (unsigned attached : {0, 1, 2}) {
            std::vector<td::OCRToken> card;
            std::vector<std::string> wanted;
            for (unsigned i = 0; i < 24; ++i) {
                const unsigned x = 40 + (i % 2) * 400, y = 50 + (i / 2) * 40;
                const auto word = i == 9 || i == 17 ? "top" : phrase[i % 12];
                wanted.push_back(word);
                if (i == 9 || attached == 0 || (attached == 1 && i % 3)) {
                    auto label = Token(i == 9 ? "top" : std::to_string(i + 1) + "-", x, y, i * 2);
                    label.width = 24;
                    if (i == 9) { label.candidates = {td::SecretBytes{'t','o','p'}}; label.confident = true; }
                    card.push_back(label); card.push_back(Token(word, x + 60, y, i * 2 + 1));
                } else {
                    auto token = Token(std::to_string(i + 1) + "-" + word, x, y, i * 2);
                    token.width = 150; token.prefix_width = 24; token.word_offset = 60;
                    card.push_back(token);
                }
            }
            if (attached == 2) {
                auto without_geometry = card;
                for (auto& token : without_geometry) token.prefix_width = token.word_offset = 0;
                Check(td::ReadSeedTokens(without_geometry).words.size() == 25, "Regression no longer exercises missing attached-prefix evidence");
            }
            std::reverse(card.begin(), card.end());
            td::CborWriter encoded; encoded.Array(card.size());
            for (const auto& token : card) {
                encoded.Array(10);
                for (unsigned n : {token.x, token.y, token.width, token.height, token.line}) encoded.UInt(n);
                encoded.Bytes(token.text); encoded.Array(token.candidates.size());
                for (const auto& candidate : token.candidates) encoded.Bytes(candidate);
                encoded.Bool(token.confident); encoded.UInt(token.prefix_width); encoded.UInt(token.word_offset);
            }
            auto result = td::DecodeSeedWords(encoded.data);
            Check(result.words.size() == 24, "Second-column 10- misread as top created a twenty-fifth slot");
            for (unsigned i = 0; i < 24; ++i) Check(Text(result.words[i].value) == wanted[i], "Numbered row order or genuine TOP word was lost");
            result.words[0].value = td::SecretBytes{'z','o','o'};
            const auto before = td::DraftMnemonic(result);
            td::SuggestSeedChecksum(result);
            Check(td::DraftMnemonic(result) == before, "Post-capture correction changed the selected reading order");
        }
        for (unsigned columns : {2, 3, 6}) for (bool row_first : {false, true}) {
            std::vector<td::OCRToken> card;
            for (unsigned row = 0; row < 12 / columns; ++row) for (unsigned col = 0; col < columns; ++col) {
                const unsigned i = row_first ? row * columns + col : col * (12 / columns) + row;
                card.push_back(Token(std::to_string(i + 1) + "." + phrase[i], 40 + col * 300, 40 + row * 50, i));
            }
            Check(Text(td::DraftMnemonic(td::ReadSeedTokens(card))) == expected, "Numbering did not select its geometric traversal");
        }
        for (unsigned clues : {3, 4}) {
            std::vector<td::OCRToken> card;
            for (unsigned i = 0; i < 12; ++i) card.push_back(Token((i < clues ? std::to_string(i + 1) + "." : "") + phrase[i],
                40 + (i % 2) * 300, 40 + (i / 2) * 50, i));
            const auto result = td::ReadSeedTokens(card);
            Check(Text(result.words[1].value) == phrase[clues == 4 ? 1 : 2], "Weak numbering changed the default or sufficient clues were ignored");
            if (clues == 4) {
                card[2].text = td::SecretBytes{'2','.','t','h','a','n','k'};
                Check(Text(td::ReadSeedTokens(card).words[1].value) == phrase[2], "Conflicting numbering chose row order");
            }
        }
        for (unsigned rows : {1, 2, 3}) {
            std::vector<td::OCRToken> paragraph;
            for (unsigned i = 0; i < 12; ++i) {
                // Three lines have staggered word gaps, without a persistent gutter.
                const unsigned x = 30 + (i % (12 / rows)) * (rows == 3 ? 100 : 150) + (rows == 3 ? (i / 4) * 30 : 0);
                paragraph.push_back(Token(phrase[i], x, 40 + (i / (12 / rows)) * 50, i));
            }
            Check(Text(td::DraftMnemonic(td::ReadSeedTokens(paragraph))) == expected, "Short lines or paragraph were read as columns");
        }
        for (unsigned columns : {2, 3}) {
            std::vector<td::OCRToken> card;
            for (unsigned i = 0; i < 12; ++i) card.push_back(Token(phrase[i], 40 + (i / (12 / columns)) * 300, 40 + (i % (12 / columns)) * 50, i));
            Check(Text(td::DraftMnemonic(td::ReadSeedTokens(card))) == expected, "Unnumbered multi-column default changed");
        }
        {
            auto card = numbered_card(1, 10, 1);
            card.erase(card.begin() + 21); // No neighboring word: retain the uncertain detection.
            auto result = td::ReadSeedTokens(card);
            Check(result.words.size() == 12 && std::count_if(result.words.begin(), result.words.end(), [](const auto& word) {
                return Text(word.value) == "art";
            }) == 2, "A label-shaped detection without a neighboring word was discarded");
            card = numbered_card(1, 10, 1);
            card[20].width = 70; // Spans the label and word areas: no whole-token deletion.
            Check(td::ReadSeedTokens(card).words.size() == 13, "A detection crossing the label boundary was discarded");
            card = numbered_card(1, 10, 1);
            card[21].y += 18;
            Check(td::ReadSeedTokens(card).words.size() == 13, "A label stole a word from a different row");
            card = numbered_card(1, 10, 1);
            card[0].text = td::SecretBytes{'1','2','.'}; // Conflicting sequence cannot establish a label band.
            Check(td::ReadSeedTokens(card).words.size() == 13, "Conflicting numbering was used to discard text");
            for (unsigned anchors : {0, 2}) {
                card = numbered_card(1, 10, 1);
                for (unsigned row = 12; row-- > anchors;) if (row != 10) card.erase(card.begin() + row * 2);
                Check(td::ReadSeedTokens(card).words.size() == 13, "Weak or absent numbering was used to discard text");
            }
            for (unsigned row : {0, 11})
                Check(td::ReadSeedTokens(numbered_card(1, row, 1)).words.size() == 13, "Label filtering extrapolated beyond recognized rows");
            card = numbered_card(2, 10, 1);
            for (size_t i = card.size(); i-- > 0;) if (card[i].x == 440 && td::ReadOCRNumber(card[i].text).value) card.erase(card.begin() + i);
            Check(td::ReadSeedTokens(card).words.size() == 25, "One column's numbering filtered a different unnumbered column");
            card = numbered_card(1, 10, 1);
            for (size_t i = card.size(); i-- > 0;) if (card[i].x == 40 && td::ReadOCRNumber(card[i].text).value) {
                card[i].text.insert(card[i].text.end(), card[i + 1].text.begin(), card[i + 1].text.end());
                card[i].width = 150; card.erase(card.begin() + i + 1);
            }
            Check(td::ReadSeedTokens(card).words.size() == 13, "Attached number+word boxes established a false label band");
            card = numbered_card(1, 10, 1);
            card[21].text = td::SecretBytes{'t','h'}; card[21].width = 20;
            auto rest = Token("ank", 122, card[21].y, card[21].line); rest.width = 35;
            card.insert(card.begin() + 22, rest);
            result = td::ReadSeedTokens(card);
            Check(result.words.size() == 12 && Text(result.words[10].value) == "thank", "Label filtering lost a recovery-word fragment");
            card = numbered_card(1, 10, 3);
            card[20].width = 6;
            card[21].text = td::SecretBytes{'l'}; card[21].x += 2; card[21].width = 6;
            result = td::ReadSeedTokens(card);
            Check(result.words.size() == 12 && Text(result.words[10].value) == "thank", "A split misread digit poisoned the label evidence");
            card = numbered_card(1, 12, 1); // Undamaged 12 rows, followed by an ordinary thirteenth entry.
            auto label = Token("13.-", 40, 530, 24); label.width = 24;
            card.push_back(label); card.push_back(Token("art", 100, 530, 25));
            Check(td::ReadSeedTokens(card).words.size() == 13, "Label filtering forced a regular list to a valid seed count");
        }
        std::vector<td::OCRToken> tokens;
        for (unsigned i = 0; i < 12; ++i) tokens.push_back(Token("abandon", 20, i * 30, i));
        tokens.back().text = td::SecretBytes{'a','b','o','u','t'};
        auto draft = td::ReadSeedTokens(tokens);
        Check(td::DraftChecksum(draft), "Repeated-word phrase was rejected");
        draft.words.back().value = td::SecretBytes{'a','b','a','n','d','o','n'};
        draft.words.back().uncertain = true;
        Check(!td::DraftChecksum(draft), "Bad checksum accepted");
        Check(td::SuggestSeedChecksum(draft) && td::DraftChecksum(draft), "Bounded suggestion did not restore candidate");
        for (auto& word : draft.words) { word.alternatives = {word.value, td::SecretBytes{'a','c','t'}}; word.uncertain = true; }
        Check(!td::SuggestSeedChecksum(draft), "Exponential candidate search was not bounded");
        for (unsigned n : {1, 11, 13, 23}) {
            std::vector<td::OCRToken> wrong(n, Token("abandon"));
            auto invalid = td::ReadSeedTokens(wrong);
            Check(!td::DraftChecksum(invalid) && !td::SuggestSeedChecksum(invalid), "Invalid count was silently repaired");
            Reject([&] { td::DraftMnemonic(invalid); });
        }
        for (unsigned n : {25, 27, 30}) {
            auto extra = td::ReadSeedTokens(std::vector<td::OCRToken>(n, Token("abandon")));
            Check(extra.words.size() == n && !td::DraftChecksum(extra) && !td::SuggestSeedChecksum(extra),
                "Extra OCR text was dropped or accepted as a valid seed");
            Reject([&] { td::DraftMnemonic(extra); });
        }
        Reject([] { td::ReadSeedTokens(std::vector<td::OCRToken>(31, Token("abandon"))); });
        std::vector<td::OCRToken> card;
        card.push_back(Token("24.word", 10, 5, 90));
        card.push_back(Token("safe", 10, 40, 91)); // A real BIP39 word used as a heading must not be blacklisted globally.
        for (unsigned i = 0; i < 24; ++i) card.push_back(Token(std::to_string(i + 1) + "." + (i == 23 ? "art" : "abandon"),
            50 + (i / 12) * 400, 150 + (i % 12) * 50, i));
        auto panel = td::ReadSeedTokens(card);
        Check(panel.words.size() == 26, "Parser silently selected an area instead of preserving the confirmed crop");
        card[13].text = td::SecretBytes{'a','b','a','n','d','o','n'};
        Check(td::ReadSeedTokens(card).words.size() == 26, "Incomplete numbering silently discarded outside content");
        for (unsigned count : {12, 15, 18, 21, 24}) {
            std::vector<td::OCRToken> annotated;
            for (unsigned i = 0; i < count; ++i)
                annotated.push_back(Token(std::to_string(i + 1) + ".abandon", 100 + (i / 12) * 300, 150 + (i % 12) * 40, i));
            // Context deliberately consists of valid BIP39 words.
            for (const auto& [x, y, line] : {std::tuple{10U, 30U, 80U}, {900U, 230U, 81U}, {10U, 900U, 82U}})
                for (unsigned j = 0; j < 3; ++j) annotated.push_back(Token("safe", x + j * 95, y, line));
            annotated.push_back(Token("2026", 1400, 30, 83));
            if (count + 9 <= td::MAX_DRAFT_WORDS) Check(td::ReadSeedTokens(annotated).words.size() == count + 9, "Context was silently discarded");
            else Reject([&] { td::ReadSeedTokens(annotated); });
        }
        std::vector<td::OCRToken> mixed;
        for (unsigned i = 0; i < 24; ++i) mixed.push_back(Token((i < 12 ? std::to_string(i + 1) + "." : "") + std::string("abandon"),
            100 + (i / 12) * 400, 150 + (i % 12) * 40, i));
        Check(td::ReadSeedTokens(mixed).words.size() == 24, "A missed-number column was truncated to a shorter phrase");
        std::vector<td::OCRToken> thirteen;
        for (unsigned i = 0; i < 13; ++i) thirteen.push_back(Token(std::to_string(i + 1) + ".abandon", 100, 100 + i * 40, i));
        Check(td::ReadSeedTokens(thirteen).words.size() == 13, "A regular 13-entry list was truncated to fit BIP39");
        std::vector<td::OCRToken> competing;
        for (unsigned side = 0; side < 2; ++side) for (unsigned i = 0; i < 12; ++i)
            competing.push_back(Token(std::to_string(i + 1) + "." + phrase[i], 50 + side * 500, 100 + i * 40, side * 12 + i));
        Check(td::ReadSeedTokens(competing).words.size() == 24, "Parser silently chose a numbered list");
        competing.back().text = td::SecretBytes{'1','2','.','a','b','a','n','d','o','n'};
        Check(td::ReadSeedTokens(competing).words.size() == 24, "A checksum match selected a text subset");
        Reject([] { td::ReadSeedTokens(std::vector<td::OCRToken>(97, Token("abandon"))); });
        Reject([] { td::ReadSeedTokens({}); });
        for (const auto prefix : {"0.", "25.", "999999999999999999999999999."}) {
            const auto bad_number = td::ReadSeedTokens({Token(prefix), Token("abandon")});
            Check(!bad_number.numbered && bad_number.number_warning && bad_number.words.size() == 1,
                "Invalid numbering was trusted or treated as a recovery word");
        }
        Reject([] { td::ReadSeedTokens({Token("\033[2J")}); });
        Reject([] { td::ReadSeedTokens({Token("abandon", 9999)}); });
        auto conflicting = td::ReadSeedTokens({Token("1.abandon"), Token("1.about", 10, 40, 1)});
        Check(!conflicting.numbered && conflicting.number_warning, "Duplicate numbering was trusted");
        const auto noisy_prefix = td::ReadSeedTokens({Token("g."), Token("abandon", 40)});
        Check(noisy_prefix.words.size() == 1 && noisy_prefix.number_warning, "Misread numbering became a seed word");
        auto gaps = td::ReadSeedTokens({Token("1.legal"), Token("3.thank", 10, 90, 2), Token("4.", 10, 130, 3)});
        Check(gaps.numbered && gaps.words.size() == 4 && gaps.words[1].value.empty() && gaps.words[3].value.empty(),
            "Numbering gaps or trailing number-only slots were lost");
        gaps = td::ReadSeedTokens({Token("1.", 10, 10, 0), Token("2.winner", 10, 60, 1)});
        Check(gaps.numbered && gaps.words.size() == 2 && gaps.words[0].value.empty(), "A new number discarded an unreadable preceding slot");
        gaps = td::ReadSeedTokens({Token("1.", 10, 10, 0), Token("winner", 40, 32, 1)});
        Check(gaps.words.size() == 2 && gaps.words[0].value.empty(), "Missing number-only slot stole the next line's word");
        gaps = td::ReadSeedTokens({Token("2.winner"), Token("3.thank", 10, 50, 1)});
        Check(gaps.words.size() == 3 && gaps.words[0].value.empty(), "Leading numbered gap was lost");
        gaps = td::ReadSeedTokens({Token("legal"), Token("", 10, 60, 1), Token("thank", 10, 110, 2)});
        Check(gaps.words.size() == 3 && gaps.words[1].value.empty() && gaps.words[1].uncertain, "Unreadable detected region disappeared");
        gaps = td::ReadSeedTokens({Token("legal"), Token("thank", 10, 150, 2)});
        Check(gaps.words.size() == 2, "Unnumbered spacing invented a word slot");
        Check(td::ReadSeedTokens({Token("24.")}).words.size() == 1, "One stray number invented a phrase");
        for (const auto separator : {".", ")", "-", "/", ":", ";", "_"}) {
            const auto prefixed = td::ReadSeedTokens({Token("1" + std::string(separator)), Token("abandon", 120, 10, 1),
                Token("2" + std::string(separator) + "about", 120, 60, 2)});
            Check(prefixed.words.size() == 2 && prefixed.numbered && prefixed.words[0].number == 1
                && Text(prefixed.words[0].value) == "abandon" && Text(prefixed.words[1].value) == "about",
                "A numeric separator became an extra word");
        }
        const auto interleaved = td::ReadSeedTokens({Token("2-", 300, 10, 0), Token("1.abandon", 20, 14, 1), Token("about", 410, 10, 0)});
        Check(interleaved.words.size() == 2 && interleaved.numbered && interleaved.words[1].number == 2
            && Text(interleaved.words[1].value) == "about", "Interleaved regions detached a prefix from its word");
        const auto split_separator = td::ReadSeedTokens({Token("2", 300, 10, 0), Token("-", 400, 10, 0),
            Token("about", 450, 10, 0), Token("1.abandon", 20, 14, 1)});
        Check(split_separator.words.size() == 2 && split_separator.numbered && Text(split_separator.words[1].value) == "about",
            "A separately recognized number separator became a blank word");
        const auto unreadable_number = td::ReadSeedTokens({Token("1.abandon"), Token("2", 10, 60, 1), Token("???", 120, 60, 1)});
        Check(unreadable_number.words.size() == 2 && unreadable_number.words[1].value.empty(), "Unpaired number lost its unreadable slot");
        const auto partial_rows = td::ReadSeedTokens({Token("1.abandon", 10, 10, 0), Token("2.ability", 410, 10, 1),
            Token("3.absent", 10, 60, 2), Token("acid", 410, 60, 3)});
        Check(!partial_rows.numbered && Text(partial_rows.words[1].value) == "ability" && Text(partial_rows.words[2].value) == "absent",
            "Two-line default was not left-to-right");
        for (const auto& [prefix_width, word_offset] : {std::pair{0U, 10U}, {20U, 10U}, {10U, 90U}}) {
            auto invalid = Token("1.art"); invalid.prefix_width = prefix_width; invalid.word_offset = word_offset;
            Reject([&] { td::ReadSeedTokens({invalid}); });
        }
        auto false_prefix = Token("top"); false_prefix.prefix_width = 10; false_prefix.word_offset = 20;
        Reject([&] { td::ReadSeedTokens({false_prefix}); });
        auto ranked = Token("carbniv");
        ranked.candidates = {td::SecretBytes{'c','a','r','b','o','n'}, td::SecretBytes{'c','a','b','i','n'}};
        auto proposed = td::ReadSeedTokens({ranked});
        Check(Text(proposed.words[0].value) == "carbon" && proposed.words[0].uncertain && proposed.words[0].alternatives.size() == 2,
            "Weak top candidate was hidden or presented as certain");
        ranked.confident = true;
        proposed = td::ReadSeedTokens({ranked});
        Check(Text(proposed.words[0].value) == "carbon" && !proposed.words[0].uncertain && Text(proposed.words[0].original) == "carbniv",
            "Supported CTC proposal or original evidence was lost");
        ranked.text = td::SecretBytes{'c','a','b','i','n'};
        ranked.confident = false;
        Check(Text(td::ReadSeedTokens({ranked}).words[0].value) == "carbon", "Valid-looking raw word bypassed CTC ranking");
        ranked.candidates.push_back(ranked.candidates.front());
        Reject([&] { td::ReadSeedTokens({ranked}); });
        ranked.candidates = {td::SecretBytes{'n','o','p','e'}};
        Reject([&] { td::ReadSeedTokens({ranked}); });
        td::CborWriter packet;
        packet.Array(1); packet.Array(10);
        for (unsigned n : {10, 20, 80, 24, 0}) packet.UInt(n);
        packet.Bytes(Bytes("carbniv")); packet.Array(1); packet.Bytes(Bytes("carbon")); packet.Bool(true);
        packet.UInt(0); packet.UInt(0);
        Check(Text(td::DecodeSeedWords(packet.data).words[0].value) == "carbon", "CTC suggestion did not cross the bounded CBOR boundary");
        packet.data.push_back(0); Reject([&] { td::DecodeSeedWords(packet.data); });
        packet.data.resize(packet.data.size() - 2); Reject([&] { td::DecodeSeedWords(packet.data); });
        std::puts("PASS: bounded OCR words, BIP39 corrections, numbered/unnumbered columns, duplicates and checksum proposals");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
