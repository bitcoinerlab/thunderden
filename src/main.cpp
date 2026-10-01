#include "application.h"
#include "wallet_qr.h"
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

unsigned AccountNumber(Terminal& terminal)
{
    const auto answer = terminal.Input("Choose an account",
        {"The first account is number 0."}, "Account number 0-100 [0]: ", 3);
    unsigned number = 0;
    if (!answer.empty()) {
        const auto* first = reinterpret_cast<const char*>(answer.data());
        const auto [end, error] = std::from_chars(first, first + answer.size(), number);
        Require(error == std::errc{} && end == first + answer.size() && number <= 100, "Invalid account number");
    }
    return number;
}

enum class PublicKeyFormat { Account, Text, HDKey };

std::pair<Path, PublicKeyFormat> PublicKeyChoice(Terminal& terminal)
{
    const int choice = terminal.Menu("Choose a public key", {AccountType(84), AccountType(86), AccountType(49), AccountType(44),
        "Native SegWit multisig (P2WSH)", "Nested SegWit multisig (P2SH-P2WSH)", "Legacy multisig (P2SH)",
        "Enter a custom path (advanced)"}, "Add Thunder Den as a signer in your wallet app.");
    const auto coin = Params().GetChainType() == ChainType::MAIN ? 0x80000000U : 0x80000001U;
    if (choice < 4) {
        constexpr unsigned purposes[]{84, 86, 49, 44};
        return {{0x80000000U | purposes[choice], coin, 0x80000000U | AccountNumber(terminal)}, PublicKeyFormat::Account};
    }
    if (choice == 6) return {{0x8000002dU}, PublicKeyFormat::Account};
    if (choice < 6) return {{0x80000030U, coin, 0x80000000U | AccountNumber(terminal),
        choice == 4 ? 0x80000002U : 0x80000001U}, PublicKeyFormat::Account};
    const auto answer = terminal.Input("Enter a custom path", {}, "Path: ", 384);
    std::string text(answer.begin(), answer.end());
    std::replace(text.begin(), text.end(), 'h', '\'');
    Path path;
    Require(ParseHDKeypath(text, path) && path.size() <= 32, "Invalid BIP32 path");
    const auto format = terminal.Menu("Choose a QR format", {"Public-key text", "HD key QR (hdkey)"},
        "Choose the QR format requested by your wallet app.");
    return {std::move(path), format == 0 ? PublicKeyFormat::Text : PublicKeyFormat::HDKey};
}

QRMessage Scan(Terminal& terminal)
{
    terminal.Screen("Scan a QR code", {"Opening the camera on this device...",
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
        std::optional<td::ApprovedWallet> loaded_wallet;
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
                "Scan a QR code", "Share a public key (xpub)", "End session (clear keys)"},
                "Scan a transaction or request from your wallet app.", false,
                loaded_wallet ? "Loaded wallet: " + loaded_wallet->policy.Name() : "");
            if (choice == 2) return 0;
            try {
                if (choice == 1) {
                    const auto [path, format] = PublicKeyChoice(terminal);
                    auto lines = td::PublicKeyReview(keys(), path);
                    if (format == PublicKeyFormat::Account) {
                        lines.push_back("Address type: " + (path.size() == 3 ? td::AccountType(path[0] & 0x7fffffffU)
                            : std::string(path.size() == 1 ? "Legacy multisig (P2SH)" : path.back() == 0x80000001U
                                ? "Nested SegWit multisig (P2SH-P2WSH)" : "Native SegWit multisig (P2WSH)")));
                        if (path.size() >= 3) lines.push_back("Account: " + std::to_string(path[2] & 0x7fffffffU));
                    }
                    lines.insert(lines.end(), {"", "Extended public key:",
                        td::EncodePublic(keys().PublicAt(path), Params().GetChainType() == ChainType::MAIN)});
                    const td::ReviewScreen review{"Share a public key", std::move(lines), {}};
                    if (terminal.Confirm(review.title, review.summary, "show the QR code")) {
                        if (format == PublicKeyFormat::Text) td::ShowQR(terminal, td::PublicKeyText(keys(), path), &review);
                        else td::ShowQR(terminal, format == PublicKeyFormat::Account ? td::PublicAccount(keys(), path)
                            : td::PublicHDKey(keys(), path), &review);
                    }
                } else if (choice == 0) {
                    keys(); // Recovery words must be entered before the online webcam faces this screen.
                    const auto message = Scan(terminal);
                    std::optional<td::QRMessage> response;
                    std::optional<td::ReviewScreen> completed;
                    const auto approve = [&](const td::TransactionReview& review) {
                        return td::ApproveTransaction(terminal, review, completed);
                    };
                    if (message.type == "crypto-psbt") {
                        response = td::SignRequest(terminal, keys(), loaded_wallet, message, [&] { return Scan(terminal); }, approve);
                    } else if (td::IsMultisigSetup(message)) {
                        LoadWallet(terminal, keys(), loaded_wallet, message);
                    } else {
                        auto result = td::HandleQRRequest(message, keys(), {
                            [&](const auto& lines) {
                                completed = td::ReviewScreen{"Public key (xpub)", lines, {}};
                                return terminal.Confirm("Share a public key (xpub)", lines, "show the QR code");
                            },
                            [&](const auto& lines, const auto& details) {
                                completed = td::ReviewScreen{"Registered wallet - review", lines, details};
                                return terminal.Approve("Confirm wallet spending rules", lines, "REGISTER", details);
                            },
                            [&](const auto& lines, const auto& details) {
                                completed = td::ReviewScreen{"Confirmed address - review", lines, details};
                                return terminal.Confirm("Check your wallet's address", lines, "confirm this address", details);
                            }, approve});
                        if (result.status) completed.reset();
                        response = std::move(result.message);
                    }
                    if (response) td::ShowQR(terminal, *response, completed ? &*completed : nullptr);
                }
            } catch (const td::Cancelled&) {
                // All scoped camera/display/input buffers are released before the menu.
            } catch (const std::exception& error) {
                terminal.Notice("Could not complete this step", {"Check the details below before trying again.", "", error.what()});
            }
        }
    } catch (const std::exception& error) { std::fprintf(stderr, "Thunder Den: %s\n", error.what()); return 1; }
}
