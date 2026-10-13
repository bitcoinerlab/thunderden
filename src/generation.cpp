#include "entropy.h"
#include "terminal.h"
#include <algorithm>

namespace td {
SecretBytes Terminal::ManualEntropy(ManualSource source, unsigned words)
{
    const bool dice = source == ManualSource::Dice, pairs = source == ManualSource::CoinPairs;
    const std::string title = dice ? "Add die rolls" : pairs ? "Add coin tosses (pairs)" : "Add coin tosses";
    if (!Confirm(title, {dice ? "Roll one six-sided die. Press 1 to 6 for each result." : "Toss a coin. Press T for heads or Y for tails.", "",
        "Use real physical results. Keep them private and do not record them with a camera.", "",
        "Press once per result. Holding a key records extra results; check the count and use Backspace to correct it."}, "start collecting",
        {pairs ? "Coin pairs reduce constant bias only under independent tosses. Equal pairs are discarded. Fair coins average four tosses per accepted bit."
            : dice ? "The collection target assumes each face has conditional probability at most 1/4. This is not measured for your die."
            : "The collection target assumes each outcome has conditional probability at most 0.54. This is not measured for your coin.", "",
            "System randomness is always required. Repeating these results will not recover the same wallet. Back up the generated words."})) throw Cancelled{};
    const auto view = View();
    const auto target = ManualTarget(source, words);
    SecretBytes raw; raw.reserve(MAX_MANUAL_TOSSES);
    Draw(view, title, {dice ? "1-6: Record a roll" : "T: Heads   Y: Tails"}, "Backspace: Correct   Esc: Cancel");
    Flush();
    while (true) {
        auto samples = ManualSamples(source, raw);
        Row(view, 2, std::string(dice ? "Rolls: " : "Tosses: ") + std::to_string(raw.size()) + (pairs ? "" : " / " + std::to_string(target)));
        Row(view, 3, pairs ? "Accepted bits: " + std::to_string(samples.size()) + " / " + std::to_string(target) : "Collection target, not a measurement.");
        Row(view, 4, samples.size() == target ? "Enter: Finish collection" : raw.size() == MAX_MANUAL_TOSSES ? "Limit reached. Correct or cancel." : "");
        const int key = Key();
        Require(View() == view, "The screen changed. Start collection again.");
        if (key == 27 || key == 3) throw Cancelled{};
        if ((key == '\r' || key == '\n') && samples.size() == target) return samples;
        if (key == 127 || key == 8) {
            if (!raw.empty()) { raw.back() = 0; raw.pop_back(); }
        } else if (samples.size() < target && raw.size() < MAX_MANUAL_TOSSES) {
            if (dice && key >= '1' && key <= '6') raw.push_back(key - '1');
            if (!dice && (key == 't' || key == 'T' || key == 'y' || key == 'Y')) raw.push_back(key == 'y' || key == 'Y');
        }
    }
}

void Terminal::BackupWords(std::span<const unsigned char> mnemonic)
{
    ValidateMnemonic(mnemonic);
    const std::string_view text(reinterpret_cast<const char*>(mnemonic.data()), mnemonic.size());
    const auto view = View();
    const size_t count = 1 + std::count(text.begin(), text.end(), ' '), per_page = view.height - 2;
    for (size_t first = 0; first < count; first += per_page) {
        const bool last = first + per_page >= count;
        Flush();
        Draw(view, "Write down your recovery words", {}, last ? "Enter: Check backup   Esc: Cancel" : "Enter: Next   Esc: Cancel");
        size_t start = 0;
        for (size_t i = 0; i < std::min(count, first + per_page); ++i) {
            const auto end = text.find(' ', start);
            if (i >= first) {
                At(view.body + i - first, view.left);
                Write(std::to_string(i + 1) + ". ");
                Write(text.substr(start, end == text.npos ? end : end - start)); // No ordinary string/screen cache of secrets.
            }
            start = end + 1;
        }
        Row(view, view.height - 1, "Copy every word in order.");
        while (true) {
            const int key = Key();
            Require(View() == view, "The screen changed. Start backup again.");
            if (key == 27 || key == 3) throw Cancelled{};
            if (key == '\r' || key == '\n') break;
        }
    }
}

SecretBytes NewMnemonic(Terminal& terminal)
{
    const unsigned words = terminal.Menu("Generate recovery words", {"12 words", "24 words"}) == 0 ? 12 : 24;
    const auto source = static_cast<ManualSource>(terminal.Menu("Add physical randomness?",
        {"No physical input", "Coin tosses", "Coin pairs (von Neumann)", "Six-sided die"}, "System randomness is always included."));
    SecretBytes manual;
    if (source != ManualSource::None) manual = ManualContribution(source, terminal.ManualEntropy(source, words));
    if (!terminal.Confirm("Ready to generate", {"Recovery words: " + std::to_string(words), "System randomness: included",
        source == ManualSource::None ? "Physical input: not selected" : "Physical input: included", "",
        "The next screen shows your new recovery words. Keep it private and write every word down.", "",
        "You will re-enter the words to check your backup before using the wallet."}, "generate words")) throw Cancelled{};
    auto mnemonic = GenerateMnemonic(words, {}, source, manual);
    while (true) {
        terminal.BackupWords(mnemonic);
        if (!terminal.Confirm("Check your backup", {"Enter the words from your written backup in the same order."}, "enter your words")) throw Cancelled{};
        if (terminal.Mnemonic(words) == mnemonic) return mnemonic;
        if (!terminal.Confirm("Backup did not match", {"Check every word and its order. Review the words again before retrying."}, "review the words")) throw Cancelled{};
    }
}
}
