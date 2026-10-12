#pragma once
#include "seed_ocr.h"

namespace td {
struct WordRanking {
    std::vector<SecretBytes> words;
    bool confident{false};
};
// Time-major CTC probabilities; zero labels are blank. No phrase/checksum search.
WordRanking RankSeedWords(std::span<const float> probabilities, std::span<const unsigned char> alphabet);
}
