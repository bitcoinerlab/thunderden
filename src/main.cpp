#include "application.h"
#include "hardware.h"
#include "isolation.h"
#include "scan.h"
#include "qr_commands.h"

#include <chainparams.h>
#include <util/strencodings.h>
#include <util/bip32.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <memory>

namespace {
using namespace td;

std::pair<unsigned, unsigned> Account(Terminal& terminal)
{
    const int choice = terminal.Menu("Choose an address type", {"Native SegWit (BIP84)", "Taproot (BIP86)",
        "Wrapped SegWit (BIP49)", "Legacy (BIP44)"}, "Choose the address type used by your wallet app.");
    constexpr unsigned purposes[]{84, 86, 49, 44};
    const auto answer = terminal.Input("Choose an account",
        {"Use the account number from your wallet app.", "The first account is number 0."}, "Account number 0-100 [0]: ", 3);
    unsigned number = 0;
    if (!answer.empty()) {
        const auto* first = reinterpret_cast<const char*>(answer.data());
        const auto [end, error] = std::from_chars(first, first + answer.size(), number);
        Require(error == std::errc{} && end == first + answer.size() && number <= 100, "Invalid account number");
    }
    return {purposes[choice], number};
}

QRMessage Scan(Terminal& terminal)
{
    terminal.Screen("Scan a wallet request", {"Opening the camera on this device...",
        "Point it at the QR code shown by your wallet app."}, "Esc: Cancel");
    Display display(terminal);
    ScanProcess scanner(ScannerExecutable());
    terminal.Flush();
    while (true) {
        const int key = terminal.Key(0);
        if (key == 27 || key == 3 || key == 'q') throw Cancelled{};
        auto update = scanner.Poll();
        if (!update) continue;
        if (update->message) return std::move(*update->message);
        display.Preview(update->gray, update->width, update->height, update->progress / 100.0);
    }
}

void Show(Terminal& terminal, const QRMessage& message)
{
    URSender sender(message);
    auto frame = sender.Next();
    const bool animated = sender.Parts() > 1;
    // Allow the full sequence-number growth while keeping every frame's geometry
    // fixed. The initial fragment is at least as large as subsequent fragments.
    const QRImage probe(std::string(std::min(MAX_QR_TEXT, frame.size() + 64), 'A'));
    const int version = (probe.width - 17) / 4;
    Display display(terminal);
    terminal.Flush();
    bool paused = false;
    while (true) {
        display.QR(QRImage(frame, version), animated
            ? std::string("Space: ") + (paused ? "Resume QR codes" : "Pause QR codes") + "   Esc: Back"
            : "Keep this code visible until scanning finishes. Esc: Back");
        const int key = terminal.Key(animated && !paused ? 250 : -1);
        if (key == 27 || key == 3 || key == 'q' || key == '\r' || key == '\n') return;
        if (animated && key == ' ') paused = !paused;
        if (animated && !paused) frame = sender.Next();
    }
}

ChainType Network(Terminal& terminal)
{
    const int choice = terminal.Menu("Choose a Bitcoin network", {"Bitcoin mainnet", "Signet", "Testnet4", "Regtest", "Legacy testnet3"},
        "Welcome. Choose the same Bitcoin network as your wallet app. Mainnet uses real bitcoin; the others are for testing.", false);
    constexpr ChainType networks[]{ChainType::MAIN, ChainType::SIGNET, ChainType::TESTNET4, ChainType::REGTEST, ChainType::TESTNET};
    return networks[choice];
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::puts("Thunder Den: local keyboard, framebuffer and webcam signer. Run on a Linux console.");
        return 0;
    }
    const bool session_ended = argc == 2 && std::string_view(argv[1]) == "--session-ended";
    if (argc != 1 && !session_ended) return 1;
    try {
        td::Terminal terminal;
        td::ConfigureConsole(terminal.FD());
        if (session_ended) {
            // The launcher starts this keyless viewer only after the old signer exits.
            td::PrepareSigner();
            return terminal.SessionEnded() ? 0 : 1;
        }
        SelectParams(Network(terminal));
        terminal.SetNetwork(td::NetworkName(Params().GetChainType()));
        td::PrepareSigner(); // Before any recovery input or private key exists.
        ECC_Context context;
        std::unique_ptr<td::Keys> session;
        const auto keys = [&]() -> const td::Keys& {
            if (!session) {
                const auto mnemonic = terminal.Mnemonic();
                while (!session) {
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
                    session = std::make_unique<td::Keys>(mnemonic, passphrase);
                }
            }
            return *session;
        };
        while (true) {
            const int choice = terminal.Menu("What would you like to do?", {
                "Scan a wallet request", "Share wallet setup (descriptor)", "Share a public key (xpub)", "End session (clear keys)"},
                "Start an action in your wallet app, then scan the QR code it shows.", false);
            if (choice == 3) return 0;
            try {
                if (choice == 1) {
                    const auto [purpose, account] = Account(terminal);
                    auto policy = td::DefaultPolicy(keys(), purpose, account);
                    const auto& info = policy.KeyInformation()[0];
                    auto details = td::PolicyDetails(policy, keys());
                    details.push_back("Full public descriptor:"); details.push_back(policy.DescriptorText());
                    if (terminal.Confirm("Share your wallet setup", {
                        "This shares the public information your wallet app needs to find your addresses and follow this account's activity.",
                        "It contains no private keys. Only share it with a wallet app you trust.", "",
                        "Network: " + td::NetworkName(Params().GetChainType()),
                        "Account: " + std::to_string(account), "Address type: " + td::AccountType(purpose),
                        "Path: " + td::PathText(info.origin), "Master fingerprint: " + HexStr(info.fingerprint)}, "show the QR code", details))
                        Show(terminal, td::PublicDescriptor(policy));
                } else if (choice == 2) {
                    const auto answer = terminal.Input("Choose a public key to share", {"Enter the derivation path provided by your wallet app.",
                        Params().GetChainType() == ChainType::MAIN ? "For example: m/84h/0h/0h" : "For example: m/84h/1h/0h"}, "Path: ", 384);
                    std::string path_text(answer.begin(), answer.end());
                    std::replace(path_text.begin(), path_text.end(), 'h', '\'');
                    std::vector<uint32_t> path;
                    td::Require(ParseHDKeypath(path_text, path) && path.size() <= 32, "Invalid BIP32 path");
                    const auto message = td::PublicHDKey(keys(), path);
                    if (terminal.Confirm("Share a public key (xpub)", td::PublicKeyReview(keys(), path), "show the QR code",
                        {td::EncodePublic(keys().PublicAt(path), Params().GetChainType() == ChainType::MAIN)})) Show(terminal, message);
                } else if (choice == 0) {
                    keys(); // Recovery words must be entered before the online webcam faces this screen.
                    const auto message = Scan(terminal);
                    std::optional<td::QRMessage> response;
                    const auto approve = [&](const td::TransactionReview& review) {
                        return terminal.Approve("Review transaction", td::TransactionLines(review), "SIGN");
                    };
                    if (message.type == "crypto-psbt") {
                        const auto bytes = td::UnwrapBytes(message.cbor);
                        td::Require(bytes.size() <= td::ReviewedTransaction::MAX_PSBT_BYTES, "PSBT size limit exceeded");
                        const auto [purpose, account] = Account(terminal);
                        const auto raw = std::as_bytes(std::span(bytes));
                        const td::ReviewedTransaction reviewed(td::DefaultPolicy(keys(), purpose, account), keys(), td::Digest{}, raw);
                        const auto result = reviewed.Sign(keys(), approve);
                        if (result) response = td::QRMessage{"crypto-psbt", td::CborBytes({
                            reinterpret_cast<const uint8_t*>(result->psbt.data()), result->psbt.size()})};
                    } else {
                        response = td::HandleQRRequest(message, keys(), {
                            [&](const auto& lines) { return terminal.Confirm("Share a public key (xpub)", lines, "show the QR code"); },
                            [&](const auto& lines) { return terminal.Approve("Confirm wallet spending rules", lines, "REGISTER"); },
                            [&](const auto& lines) { return terminal.Confirm("Check your wallet's address", lines, "confirm this address"); }, approve});
                    }
                    if (response) Show(terminal, *response);
                }
            } catch (const td::Cancelled&) {
                // All scoped camera/display/input buffers are released before the menu.
            } catch (const std::exception& error) {
                terminal.Notice("Could not complete this step", {"Check the details below before trying again.", "", error.what()});
            }
        }
    } catch (const std::exception& error) { std::fprintf(stderr, "Thunder Den: %s\n", error.what()); return 1; }
}
