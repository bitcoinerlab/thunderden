#pragma once
#include "keys.h"

namespace td {
inline constexpr size_t MAX_OCR_TOKENS = 96, MAX_SEED_WORDS = 24, MAX_DRAFT_WORDS = 30, MAX_WORD_SUGGESTIONS = 5;
inline constexpr std::string_view OCR_NUMBER_SEPARATORS = ".):;]-/?*,'_ ";
struct OCRNumber { unsigned value{}; size_t length{}; };
// A zero length means no numeric prefix; zero value with a length is invalid numbering.
OCRNumber ReadOCRNumber(std::span<const unsigned char> text);
struct OCRToken {
    unsigned x{}, y{}, width{}, height{}, line{};
    SecretBytes text;
    std::vector<SecretBytes> candidates{};
    bool confident{false};
    unsigned prefix_width{}, word_offset{}; // Attached prefix bounds from character alignment; otherwise zero.
};
struct SeedWord {
    SecretBytes original, value;
    std::vector<SecretBytes> alternatives;
    unsigned x{}, y{}, width{}, height{}, number{};
    bool uncertain{false};
};
struct SeedDraft {
    std::vector<SeedWord> words;
    bool numbered{}, number_warning{}, checksum_suggested{};
};
bool SeedWordCount(size_t count);
std::vector<SecretBytes> WordSuggestions(std::span<const unsigned char> text);
SeedDraft ReadSeedTokens(const std::vector<OCRToken>& tokens);
SeedDraft DecodeSeedWords(std::span<const uint8_t> packet);
void OrderSeedWords(SeedDraft& draft);
SecretBytes DraftMnemonic(const SeedDraft& draft);
bool DraftChecksum(const SeedDraft& draft);
bool SuggestSeedChecksum(SeedDraft& draft);
}
