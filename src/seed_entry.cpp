#include "seed_entry.h"
#include "hardware.h"
#include "scan.h"
#include <support/cleanse.h>

namespace td {
namespace {
SecretBytes CaptureWords(Terminal& terminal)
{
    while (true) {
        terminal.Screen("Read recovery words", {"Opening the camera. Press Enter to capture, then select the recovery words.",
            "Words and photos stay on this device."}, "Esc: Cancel");
        Display display(terminal);
        ScanProcess scanner(ScannerExecutable("thunderden-ocr"), true);
        bool ready = false, captured = false;
        terminal.Flush();
        try {
            while (true) {
                const int key = terminal.Key(0);
                if (key == 27 || key == 3) throw Cancelled{};
                if (ready && !captured && (key == '\r' || key == '\n')) {
                    scanner.CaptureSeed(); captured = true;
                    display.Clear();
                    display.Caption({"Preparing the captured image...", "Esc: Cancel"});
                }
                auto update = scanner.Poll();
                if (!update) continue;
                if (update->seed) { display.Clear(); return std::move(*update->seed); }
                if (!update->gray.empty()) {
                    if (update->area) {
                        try {
                            const auto selected = display.SelectArea(terminal, update->gray, update->width, update->height, *update->area);
                            scanner.ReadSeedArea(selected);
                        } catch (const Cancelled&) {
                            memory_cleanse(update->gray.data(), update->gray.size()); break;
                        } catch (...) {
                            memory_cleanse(update->gray.data(), update->gray.size()); throw;
                        }
                        display.Clear(); display.Caption({"Reading the selected words...", "Esc: Cancel"});
                    } else if (!captured) display.Preview(update->gray, update->width, update->height, 0,
                        {"Try to fill the camera view with the words.", "Enter: Capture  Esc: Cancel"});
                    memory_cleanse(update->gray.data(), update->gray.size());
                    ready = true;
                }
            }
            display.Clear();
        } catch (...) { display.Clear(); throw; }
    }
}

}

std::optional<SecretBytes> ReviewSeedWords(Terminal& terminal, SeedDraft& draft)
{
    size_t selected = 0;
    while (true) {
        switch (terminal.EditSeedWords(draft, selected)) {
        case SeedAction::Use: {
            auto mnemonic = DraftMnemonic(draft); ValidateMnemonic(mnemonic); return mnemonic;
        }
        case SeedAction::Recapture: return {};
        case SeedAction::Manual:
            try { return terminal.Mnemonic(); }
            catch (const Cancelled&) { break; }
        }
    }
}

SecretBytes ReadRecoveryWords(Terminal& terminal)
{
    if (terminal.Menu("Enter recovery words", {"Type with keyboard", "Read with camera (OCR)"}) == 0) return terminal.Mnemonic();
    std::optional<SeedDraft> draft;
    while (true) {
        try {
            auto captured = DecodeSeedWords(CaptureWords(terminal));
            draft = std::move(captured);
        } catch (const Cancelled&) {
            if (!draft) throw;
        } catch (const std::invalid_argument& error) {
            terminal.Notice("Could not read recovery words", {error.what()});
        } catch (const std::runtime_error&) {
            terminal.Notice("Camera unavailable", {"Try another capture or use the keyboard."});
        }
        if (draft) {
            auto mnemonic = ReviewSeedWords(terminal, *draft);
            if (mnemonic) return std::move(*mnemonic);
        } else if (terminal.Menu("Recovery words", {"Try another capture", "Type with keyboard"}) == 1) return terminal.Mnemonic();
    }
}
}
