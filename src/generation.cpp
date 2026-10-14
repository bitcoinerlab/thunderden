#include "entropy.h"
#include "terminal.h"
#include <algorithm>

namespace td {
SecretBytes Terminal::ManualEntropy(ManualSource source, unsigned words)
{
    const bool dice = source == ManualSource::Dice;
    const std::string title = dice ? "Add dice rolls" : "Add coin tosses";
    if (!Confirm(title, {dice ? "Roll one six-sided die. Press 1 to 6 for each result." : "Toss a coin. Press H for heads or T for tails.", "",
        "System randomness is the main source. These results are an optional extra.", "",
        "Use real physical results. Keep them private and do not record them with a camera.", "",
        "Press once per result. Holding a key records extra results; check the count and use Backspace to correct it."}, "start collecting",
        {dice ? "The target assumes no face is more than 1-in-4 likely on each roll, even when earlier rolls are known."
            : "The target assumes neither heads nor tails is more than 54-in-100 likely on each toss, even when earlier tosses are known.", "",
            "This is an assumption, not a test of your coin or die. Record every result.", "",
            "Repeating these results will not recover the same wallet. Back up the generated words."})) throw Cancelled{};
    const auto view = View();
    const auto target = ManualTarget(source, words);
    SecretBytes raw; raw.reserve(target);
    Draw(view, title, {dice ? "1-6: Record a roll" : "H: Heads   T: Tails", "", "", "Collection target, not a measurement."}, "Backspace: Correct   Esc: Cancel");
    Flush();
    while (true) {
        Row(view, 2, std::string(dice ? "Rolls: " : "Tosses: ") + std::to_string(raw.size()) + " / " + std::to_string(target));
        Row(view, 4, raw.size() == target ? "Enter: Finish collection" : "");
        const int key = Key();
        Require(View() == view, "The screen changed. Start collection again.");
        if (key == 27 || key == 3) throw Cancelled{};
        if ((key == '\r' || key == '\n') && raw.size() == target) return raw;
        if (key == 127 || key == 8) {
            if (!raw.empty()) { raw.back() = 0; raw.pop_back(); }
        } else if (raw.size() < target) {
            if (dice && key >= '1' && key <= '6') raw.push_back(key - '1');
            if (!dice && (key == 'h' || key == 'H' || key == 't' || key == 'T')) raw.push_back(key == 't' || key == 'T');
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
    const bool use_camera = terminal.Menu("Add camera samples?", {"No camera", "Use camera"}) == 1;
    constexpr ManualSource sources[]{ManualSource::None, ManualSource::Coins, ManualSource::Dice};
    const auto source = sources[terminal.Menu("Add physical randomness?",
        {"No physical input", "Coin tosses", "Dice rolls"}, "System randomness is the main source. Physical input is optional.")];
    SecretBytes camera, manual;
    while (use_camera) {
        try { camera = terminal.CameraEntropy(); break; }
        catch (const std::exception& error) {
            if (terminal.Menu("Camera not included", {"Continue without camera", "Try camera again"},
                std::string(error.what()) + ". No camera samples will be used.", true, {}, true) == 0) break;
        }
    }
    if (source != ManualSource::None) manual = ManualContribution(source, terminal.ManualEntropy(source, words));
    if (!terminal.Confirm("Ready to generate", {"Recovery words: " + std::to_string(words), "System randomness: included",
        source == ManualSource::None ? "Physical input: not selected" : "Physical input: included",
        !camera.empty() ? "Camera: included" : use_camera ? "Camera: omitted after failure" : "Camera: not selected", "",
        "The next screen shows your new recovery words. Keep it private and write every word down.", "",
        "You will re-enter the words to check your backup before using the wallet."}, "generate words")) throw Cancelled{};
    auto mnemonic = GenerateMnemonic(words, camera, source, manual);
    while (true) {
        terminal.BackupWords(mnemonic);
        if (!terminal.Confirm("Check your backup", {"Enter the words from your written backup in the same order."}, "enter your words")) throw Cancelled{};
        if (terminal.Mnemonic(words) == mnemonic) return mnemonic;
        if (!terminal.Confirm("Backup did not match", {"Check every word and its order. Review the words again before retrying."}, "review the words")) throw Cancelled{};
    }
}
}
