#include "entropy.h"
#include "terminal.h"
#include "hardware.h"
#include <algorithm>

namespace td {
SecretBytes Terminal::ManualEntropy(ManualSource source, unsigned words)
{
    const bool dice = source == ManualSource::Dice;
    const std::string title = dice ? "Add dice rolls" : "Add coin tosses";
    const auto target = ManualTarget(source, words);
    if (!Confirm(title, {dice ? "Roll your six-sided die, then press the number it shows." : "Toss your coin, then press H for heads or T for tails.", "",
        std::string(dice ? "Enter every roll" : "Enter every toss") + ", including repeated results. Continue until you reach "
            + std::to_string(target) + (dice ? " rolls." : " tosses."), "",
        "Keep your results private. If you make a typing mistake, Backspace removes the last entry."}, "start collecting",
        {dice ? "The target assumes no face is more than 1-in-4 likely on each roll, even when earlier rolls are known."
            : "The target assumes neither heads nor tails is more than 54-in-100 likely on each toss, even when earlier tosses are known.", "",
            "This is an assumption, not a test of your coin or die. Record every result.", "",
            "Repeating these results will not recover the same wallet. Back up the generated words."})) throw Cancelled{};
    const auto view = View();
    SecretBytes raw; raw.reserve(target);
    Draw(view, title, {"", "", dice ? "1-6: Enter a roll" : "H: Heads   T: Tails"}, "Backspace: Undo last entry   Esc: Cancel");
    Flush();
    while (true) {
        Row(view, 0, std::string(dice ? "Rolls entered: " : "Tosses entered: ") + std::to_string(raw.size()) + " of " + std::to_string(target));
        Row(view, 4, raw.size() == target ? "Enter: Continue" : "");
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
    if (ShowRecoveryWords(*this, mnemonic)) return;
    const std::string_view text(reinterpret_cast<const char*>(mnemonic.data()), mnemonic.size());
    const auto view = View();
    const size_t count = 1 + std::count(text.begin(), text.end(), ' '), per_page = view.height - 2;
    for (size_t first = 0; first < count; first += per_page) {
        const bool last = first + per_page >= count;
        const auto draw = [&] {
            Flush();
            Draw(view, "Write down your recovery words", {}, last ? "Q: Seed QR   Enter: Check backup   Esc: Cancel" : "Q: Seed QR   Enter: Next   Esc: Cancel");
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
        };
        draw();
        while (true) {
            const int key = Key();
            Require(View() == view, "The screen changed. Start backup again.");
            if (key == 27 || key == 3) throw Cancelled{};
            if (key == 'q' || key == 'Q') { ShowSeedQR(*this, mnemonic); draw(); }
            if (key == '\r' || key == '\n') break;
        }
    }
}

SecretBytes NewMnemonic(Terminal& terminal)
{
    const unsigned words = terminal.Menu("How many recovery words?", {"12 words - recommended", "24 words"},
        "12 words provide strong protection for Bitcoin and are easier to write down and check. "
        "24 words make your recovery phrase even stronger, but take longer to write down and check.", true, {}, true,
        {"12 words contain 128 bits of randomness; 24 words contain 256 bits. Both provide strong protection when generated securely."}) == 0 ? 12 : 24;
    if (!terminal.Confirm("How your recovery words are created", {
        "Thunder Den uses Bitcoin Core's random-number generator, which combines several sources of randomness from your computer.", "",
        "This is normally all you need. You can also add randomness using your camera, coin tosses or dice rolls. "
        "These extra sources may help if another source turns out to be weaker than expected."}, "continue")) throw Cancelled{};
    const bool use_camera = terminal.Menu("Add randomness from your camera?", {"Use camera", "Skip camera"},
        "Camera images contain tiny variations, including noise from the sensor. Thunder Den can mix these readings into the randomness used to create your recovery words.",
        true, {}, true) == 0;
    constexpr ManualSource sources[]{ManualSource::None, ManualSource::Coins, ManualSource::Dice};
    const auto source = sources[terminal.Menu("Add coin tosses or dice rolls?",
        {"Continue without coins or dice", "Coin tosses - " + std::to_string(ManualTarget(ManualSource::Coins, words)) + " tosses",
            "Dice rolls - " + std::to_string(ManualTarget(ManualSource::Dice, words)) + " rolls"},
        "You can add another source of randomness by tossing a coin or rolling a six-sided die and entering each result. This step is optional.",
        true, {}, true)];
    SecretBytes camera, manual;
    bool camera_unchecked = false;
    while (use_camera) {
        try { camera = terminal.CameraEntropy(camera_unchecked); break; }
        catch (const std::exception& error) {
            if (terminal.Menu("Camera data cannot be used", {"Continue without camera", "Try camera again"},
                std::string(error.what()) + ". No camera data will be added. Fresh randomness from Bitcoin Core will still be used.", true, {}, true) == 0) break;
        }
    }
    if (source != ManualSource::None) manual = ManualContribution(source, terminal.ManualEntropy(source, words));
    if (!terminal.Confirm("Ready to generate", {"Recovery words: " + std::to_string(words), "Random-number generator: Bitcoin Core",
        !camera.empty() ? camera_unchecked ? "Camera readings: included (unchecked)" : "Camera readings: included"
            : use_camera ? "Camera readings: not included" : "Camera readings: not selected",
        source == ManualSource::None ? "Coins or dice: not selected"
            : std::string(source == ManualSource::Coins ? "Coin tosses: " : "Dice rolls: ") + std::to_string(ManualTarget(source, words)), "",
        "The next screen will show your recovery words.", "",
        "Before continuing, make sure no one else can see the screen and no camera is pointed at it.", "",
        "The seed QR is another copy of these recovery words. Keep it private too. Use only a trusted offline device to scan it.", "",
        "Write down every word in order. Afterward, you'll enter them again to check your written backup."}, "generate words")) throw Cancelled{};
    auto mnemonic = GenerateMnemonic(words, camera, source, manual);
    while (true) {
        terminal.BackupWords(mnemonic);
        if (!terminal.Confirm("Check your backup", {"Enter every word from your written backup in the same order."}, "enter your words")) throw Cancelled{};
        if (terminal.Mnemonic(words) == mnemonic) return mnemonic;
        if (!terminal.Confirm("Backup did not match", {"Your entry did not match. Review the words and check your written backup before trying again."}, "review the words")) throw Cancelled{};
    }
}
}
