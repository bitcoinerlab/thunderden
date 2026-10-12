#include "ocr_ctc.h"
#include "english.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace td {
namespace {
using Probabilities = std::array<double, 53>; // Blank, a-z, A-Z.
constexpr double IMPOSSIBLE = -std::numeric_limits<double>::infinity();

double Score(std::span<const Probabilities> probabilities, std::string_view word)
{
    // One blank plus lower/upper-case states for each character. Keep cases
    // separate until CTC collapse: e.g. "aLl" can emit "all" without a blank.
    const size_t states = 3 * word.size() + 1;
    std::array<unsigned, 25> labels{};
    for (size_t i = 0; i < word.size(); ++i) {
        labels[3 * i + 1] = word[i] - 'a' + 1;
        labels[3 * i + 2] = word[i] - 'a' + 27;
    }
    std::array<double, 25> previous{}, next{};
    previous[0] = 1;
    double score = 0;
    for (const auto& p : probabilities) {
        double scale = 0;
        for (size_t s = 0; s < states; ++s) {
            double sum = previous[s];
            if (s % 3 == 0) {
                if (s) sum += previous[s - 1] + previous[s - 2];
            } else {
                const size_t blank = s - s % 3;
                sum += previous[blank];
                if (blank) for (size_t from : {blank - 2, blank - 1})
                    if (labels[from] != labels[s]) sum += previous[from];
            }
            next[s] = sum * p[labels[s]];
            scale = std::max(scale, next[s]);
        }
        if (!scale) return IMPOSSIBLE;
        score += std::log(scale);
        for (size_t s = 0; s < states; ++s) previous[s] = next[s] / scale;
    }
    return score + std::log(previous[states - 1] + previous[states - 2] + previous[states - 3]);
}
}

WordRanking RankSeedWords(std::span<const float> input, std::span<const unsigned char> alphabet)
{
    Require(!alphabet.empty() && alphabet.size() <= 1024 && alphabet[0] == 0
        && !input.empty() && input.size() % alphabet.size() == 0 && input.size() / alphabet.size() <= 256,
        "Invalid CTC probabilities");
    std::vector<Probabilities> probabilities(input.size() / alphabet.size());
    double greedy = 0;
    for (size_t t = 0; t < probabilities.size(); ++t) {
        double largest = 0;
        for (size_t c = 0; c < alphabet.size(); ++c) {
            const double p = input[t * alphabet.size() + c];
            Require(std::isfinite(p) && p >= 0 && p <= 1.00001, "Invalid CTC probability");
            largest = std::max(largest, p);
            const auto ch = alphabet[c];
            if (!ch) probabilities[t][0] += p;
            else if (ch >= 'a' && ch <= 'z') probabilities[t][ch - 'a' + 1] += p;
            else if (ch >= 'A' && ch <= 'Z') probabilities[t][ch - 'A' + 27] += p;
        }
        if (!largest) return {};
        greedy += std::log(largest);
    }
    std::array<std::pair<double, std::string_view>, MAX_WORD_SUGGESTIONS> best;
    for (auto& entry : best) entry.first = IMPOSSIBLE;
    for (const auto word : ENGLISH) {
        std::pair<double, std::string_view> entry{Score(probabilities, word), word};
        for (auto& place : best) if (entry.first > place.first) std::swap(entry, place);
    }
    WordRanking result;
    for (const auto& [score, word] : best)
        if (std::isfinite(score)) result.words.emplace_back(word.begin(), word.end());
    // Confidence needs both a clear margin and support relative to the original
    // character path. This marks uncertainty, not whether candidates are shown.
    result.confident = !result.words.empty() && best[0].first - best[1].first >= std::log(10.0)
        && best[0].first >= greedy - std::log(10.0);
    return result;
}
}
