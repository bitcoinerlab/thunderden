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
    terminal.Flush();
    terminal.Screen("Choose default account", {"1: BIP84 - Native SegWit", "2: BIP86 - Taproot",
        "3: BIP49 - Wrapped SegWit", "4: BIP44 - Legacy", "Esc: Cancel"});
    int choice;
    do { choice = terminal.Key(); } while (choice != 27 && choice != 3 && (choice < '1' || choice > '4'));
    if (choice == 27 || choice == 3) throw Cancelled{};
    constexpr unsigned purposes[]{84, 86, 49, 44};
    const auto answer = terminal.Input("Account number 0-100 [0]: ", 3, false);
    unsigned number = 0;
    if (!answer.empty()) {
        const auto* first = reinterpret_cast<const char*>(answer.data());
        const auto [end, error] = std::from_chars(first, first + answer.size(), number);
        Require(error == std::errc{} && end == first + answer.size() && number <= 100, "Invalid account number");
    }
    return {purposes[choice - '1'], number};
}

QRMessage Scan(Terminal& terminal)
{
    terminal.Screen("Scan UR v2", {"Opening webcam...", "Esc: Cancel"});
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
            ? "UR v2 / " + std::to_string(sender.Parts()) + " parts   Space: " + (paused ? "Resume" : "Pause") + "   Esc: Back"
            : "QR code   Esc: Back");
        const int key = terminal.Key(animated && !paused ? 250 : -1);
        if (key == 27 || key == 3 || key == 'q' || key == '\r' || key == '\n') return;
        if (animated && key == ' ') paused = !paused;
        if (animated && !paused) frame = sender.Next();
    }
}

ChainType Network(Terminal& terminal)
{
    terminal.Flush();
    terminal.Screen("Thunder Den - Select network", {"1: Testnet4", "2: Mainnet", "3: Signet",
        "4: Regtest", "5: Legacy testnet3"});
    while (true) {
        switch (terminal.Key()) {
        case '1': return ChainType::TESTNET4;
        case '2': return ChainType::MAIN;
        case '3': return ChainType::SIGNET;
        case '4': return ChainType::REGTEST;
        case '5': return ChainType::TESTNET;
        }
    }
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::puts("Thunder Den: local keyboard, framebuffer and webcam signer. Run on a Linux console.");
        return 0;
    }
    if (argc != 1) return 1;
    try {
        td::Terminal terminal;
        SelectParams(Network(terminal));
        td::PrepareSigner(); // Before any recovery input or private key exists.
        ECC_Context context;
        std::unique_ptr<td::Keys> session;
        const auto keys = [&]() -> const td::Keys& {
            if (!session) {
                const auto mnemonic = terminal.Mnemonic();
                while (!session) {
                    terminal.Screen("BIP39 passphrase", {"Optional printable ASCII passphrase.", "Every space and letter case is significant.", "Leave empty for no passphrase."});
                    const auto passphrase = terminal.Input("Passphrase: ", 128, true);
                    if (!passphrase.empty() && passphrase != terminal.Input("Repeat passphrase: ", 128, true)) {
                        terminal.Notice("Passphrases did not match", {"Enter the passphrase again."});
                        continue;
                    }
                    session = std::make_unique<td::Keys>(mnemonic, passphrase);
                }
            }
            return *session;
        };
        while (true) {
            terminal.Flush();
            terminal.Screen("Thunder Den - " + ChainTypeToString(Params().GetChainType()), {
                "1: Scan request or transaction", "2: Export descriptor", "3: End session (clear keys)", "4: Export xpub"});
            int choice;
            do { choice = terminal.Key(); } while (choice < '1' || choice > '4');
            if (choice == '3') return 0;
            try {
                if (choice == '2') {
                    const auto [purpose, account] = Account(terminal);
                    auto policy = td::DefaultPolicy(keys(), purpose, account);
                    const auto& info = policy.KeyInformation()[0];
                    auto details = td::PolicyReview(policy, keys());
                    details.push_back("Full public descriptor:"); details.push_back(policy.DescriptorText());
                    if (terminal.Confirm("Export descriptor", {"Network: " + ChainTypeToString(Params().GetChainType()),
                        "Account: " + std::to_string(account), "Type: BIP" + std::to_string(purpose),
                        "Path: " + td::PathText(info.origin), "Fingerprint: " + HexStr(info.fingerprint),
                        "This export can reveal account activity.", "No private keys are shared."}, "Show QR", details))
                        Show(terminal, td::PublicDescriptor(policy));
                } else if (choice == '4') {
                    const auto answer = terminal.Input("BIP32 path (for example m/48h/1h/0h/2h): ", 384, false);
                    std::vector<uint32_t> path;
                    td::Require(ParseHDKeypath(std::string(answer.begin(), answer.end()), path) && path.size() <= 32, "Invalid BIP32 path");
                    const auto message = td::PublicHDKey(keys(), path);
                    if (terminal.Confirm("Export xpub", {"Network: " + ChainTypeToString(Params().GetChainType()),
                        "Path: " + td::PathText(path), "Fingerprint: " + HexStr(keys().RootFingerprint()),
                        "This public key can reveal account activity.", "No private keys are shared."}, "Show QR",
                        {td::EncodePublic(keys().PublicAt(path), Params().GetChainType() == ChainType::MAIN)})) Show(terminal, message);
                } else if (choice == '1') {
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
                            [&](const auto& lines) { return terminal.Confirm("Share public key", lines, "Show QR"); },
                            [&](const auto& lines) { return terminal.Approve("Register wallet policy", lines, "REGISTER"); },
                            [&](const auto& lines) { return terminal.Confirm("Check address", lines, "Confirm address"); }, approve});
                    }
                    if (response) Show(terminal, *response);
                }
            } catch (const td::Cancelled&) {
                // All scoped camera/display/input buffers are released before the menu.
            } catch (const std::exception& error) {
                terminal.Notice("Operation stopped", {error.what()});
            }
        }
    } catch (const std::exception& error) { std::fprintf(stderr, "Thunder Den: %s\n", error.what()); return 1; }
}
