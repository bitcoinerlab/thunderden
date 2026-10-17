#include "application.h"
#include "terminal.h"
#include "seed_qr.h"
#include "entropy.h"
#include <algorithm>

namespace td {
bool Terminal::ReviewRecoveryWords(std::span<const unsigned char> mnemonic, const ReviewScreen& review)
{
    const auto count = 1 + std::count(mnemonic.begin(), mnemonic.end(), ' ');
    const bool qr = count == 12 || count == 24;
    auto lines = review.summary;
    lines.insert(lines.end(), {"", qr ? "Press S to show recovery words and QR on this screen." : "Press S to show recovery words on this screen."});
    return Pages(review.title, lines, "continue", review.details, PageMode::Confirm, false, {},
        [&] { BackupWords(mnemonic, true); }, qr ? "S: Show words/QR" : "S: Show words");
}

std::unique_ptr<Keys> StartSession(Terminal& terminal, const std::function<QRMessage()>& scan)
{
    while (true) {
        try {
            const auto choice = terminal.Menu("Start a session", {"Scan a seed QR", "Enter recovery words", "Create a new wallet"},
                "Load your recovery words, or create a new wallet.", false);
            SecretBytes mnemonic;
            ReviewScreen review;
            if (choice == 0) {
                const auto message = scan();
                Require(IsSeedQR(message), "This isn't a seed QR. Scan a SeedQR or CompactSeedQR backup, or enter your recovery words instead.");
                mnemonic = DecodeSeedQR(message);
                const auto count = 1 + std::count(mnemonic.begin(), mnemonic.end(), ' ');
                review = {"Recovery words scanned", {
                    "This QR contains " + std::to_string(count) + " recovery words. They are the secret used to restore your wallet.", "",
                    "Continue to use them for this session.", "", "If your wallet uses a passphrase, you'll enter it next."},
                    {message.type == "seedqr" ? "Format: Standard SeedQR" : "Format: CompactSeedQR", "Language: English BIP39",
                        "A seed QR does not include your wallet passphrase or multisig wallet setup."}};
            } else mnemonic = choice == 1 ? terminal.Mnemonic() : NewMnemonic(terminal);
            if (choice == 1) {
                const auto count = 1 + std::count(mnemonic.begin(), mnemonic.end(), ' ');
                review = {"Recovery words entered", {"You entered " + std::to_string(count) + " recovery words.", "",
                    "Continue to use them for this session.", "", "If your wallet uses a passphrase, you'll enter it next."}, {}};
            }
            if (choice != 2 && !terminal.ReviewRecoveryWords(mnemonic, review)) continue;
            while (true) {
                SecretInput visibility;
                const auto passphrase = terminal.Input("Wallet passphrase (optional)", {
                    "Some wallets use an extra word or phrase in addition to the recovery words. Enter it only if you already use one.", "",
                    "If you do not use a passphrase, leave this blank and press Enter to skip.", "",
                    "Spaces and capital letters matter. A different passphrase opens a different wallet."}, "Passphrase: ", 128, &visibility);
                if (!passphrase.empty()) {
                    visibility.visible = false;
                    if (passphrase != terminal.Input("Repeat your passphrase", {"Enter the same passphrase again to check for typing mistakes."},
                        "Repeat passphrase: ", 128, &visibility)) {
                        terminal.Notice("Passphrases did not match", {"Enter the passphrase again."});
                        continue;
                    }
                }
                return std::make_unique<Keys>(mnemonic, passphrase);
            }
        } catch (const Cancelled&) {
            // Dropping this attempt also wipes scanned/entered recovery words.
        } catch (const std::exception& error) {
            terminal.Notice("Could not load recovery words", {"Check the details below before trying again.", "", error.what()});
        }
    }
}

bool EndSessionForSeed(Terminal& terminal, const QRMessage& message)
{
    // Independently validate untrusted scanner output, without creating new keys.
    (void)DecodeSeedQR(message);
    return terminal.Menu("Recovery words already loaded", {"Keep current session", "End session and scan another seed"},
        "To use different recovery words, end this session first. "
        "This clears the current keys and loaded wallet setup from Thunder Den. Your Bitcoin and backups are not affected. "
        "You'll then scan the seed QR again.", true, {}, true) == 1;
}
}
