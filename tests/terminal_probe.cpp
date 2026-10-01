#include "terminal.h"
#include "multisig_fixture.h"
#include <util/strencodings.h>
#include <unistd.h>
#include <cstdio>
#include <stdexcept>

int main(int argc, char** argv)
{
    try {
        td::Terminal terminal(dup(STDIN_FILENO));
        if (argc == 2 && std::string_view(argv[1]).starts_with("workflow-")) {
            const std::string mode = argv[1];
            ECC_Context context;
            SelectParams(ChainType::REGTEST);
            td::Keys alice(fixture::Bytes(fixture::WORDS), {}), bob(fixture::Bytes(fixture::WORDS), fixture::Bytes("cosigner"));
            const auto setup = fixture::Setup(2, 2, {&alice, &bob}, {fixture::Path(2), fixture::Path(2, 7)});
            std::optional<td::ApprovedWallet> loaded;
            if (mode == "workflow-replace") {
                auto old = td::ImportMultisig(fixture::Setup(0, 2, {&alice, &bob}, {fixture::Path(0), fixture::Path(0)}));
                const auto id = old.ID();
                loaded.emplace(td::ApprovedWallet{std::move(old), alice.RegistrationTag(id)});
                const bool replaced = td::LoadWallet(terminal, alice, loaded, setup);
                td::Require((loaded->policy.ID() == id) == !replaced, "Cancelled setup replaced the loaded wallet");
                std::puts(replaced ? "REPLACED" : "KEPT");
                return 0;
            }
            auto first = td::DefaultPolicy(alice, 84, 0), second = td::DefaultPolicy(alice, 84, 1);
            auto multisig = td::ImportMultisig(setup);
            auto psbt = mode == "workflow-inline" || mode == "workflow-inferred" ? fixture::Spend({&multisig}, true)
                : fixture::Spend({&first, mode == "workflow-many" ? &second : &first}, mode == "workflow-fee");
            if (mode == "workflow-inferred") fixture::AccountKeys(psbt, {&multisig});
            auto foreign = td::DefaultPolicy(bob, 84, 0);
            if (mode == "workflow-unmatched") psbt = fixture::Spend({&foreign});
            const auto bytes = fixture::Serialize(psbt);
            const td::QRMessage request{"crypto-psbt", td::CborBytes({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()})};
            std::optional<td::ReviewScreen> completed;
            unsigned scans = 0;
            auto response = td::SignRequest(terminal, alice, loaded, request, [&] { ++scans; return setup; },
                [&](const auto& review) { return td::ApproveTransaction(terminal, review, completed); });
            td::Require(scans <= 1, "Setup caused an unnecessary transaction rescan");
            if (mode == "workflow-inferred" || mode == "workflow-unmatched") td::Require(scans == 0, "Request made an unnecessary setup scan");
            if (response) {
                const auto raw = td::UnwrapBytes(response->cbor);
                PartiallySignedTransaction signed_psbt;
                std::string error;
                td::Require(DecodeRawPSBT(signed_psbt, std::as_bytes(std::span(raw)), error), "Bad signed reply");
                size_t signatures = 0;
                for (const auto& input : signed_psbt.inputs) signatures += input.partial_sigs.size();
                td::Require(signatures > 0, "No signature in reply");
                if (mode == "workflow-many") td::Require(signatures == 1, "Signed multiple policies in one pass");
                if (completed->warning) terminal.Revisit(*completed);
            }
            std::printf("%s %s\n", loaded ? "LOADED" : "EMPTY", response ? "SIGNED" : "CANCELLED");
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "input") {
            td::SecretInput visibility;
            const auto secret = terminal.Input("Secret input test", {"Enter public test data"}, "Secret: ", 32, &visibility);
            std::puts(HexStr(secret).c_str()); // Test-only public fixture output.
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "mnemonic") {
            const auto mnemonic = terminal.Mnemonic();
            std::puts(HexStr(mnemonic).c_str()); // Test-only public fixture output.
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "menu") {
            const int choice = terminal.Menu("Choose an action", {"First action", "Second action", "Third action"});
            std::printf("%d\n", choice);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "menu-loaded") {
            const auto selected = terminal.Menu("Choose a public key", {"Native SegWit multisig (P2WSH)",
                "Nested SegWit multisig (P2SH-P2WSH)", "Legacy multisig (P2SH)"}, {}, true,
                "Loaded wallet: Native SegWit multisig (2 of 2)");
            std::printf("%d\n", selected);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "details") {
            const bool accepted = terminal.Confirm("Check account", {"Account summary"}, "finish", {"Technical details"});
            return accepted ? 0 : 2;
        }
        if (argc == 2 && std::string_view(argv[1]) == "details-paged") {
            td::ReviewLines summary(9, "Account summary");
            return terminal.Confirm("Check account", summary, "finish", {"Technical details"}) ? 0 : 2;
        }
        td::ReviewLines lines;
        for (int i = 0; i < 11; ++i) lines.push_back("Review field " + std::to_string(i));
        lines.push_back(std::string(158, 'x') + "ADDRESS-END");
        td::ReviewLines details;
        if (argc == 2 && (std::string_view(argv[1]) == "approve-details" || std::string_view(argv[1]) == "completed-review"))
            details = {"Wallet ID: " + std::string(64, 'a'), "Full public descriptor:", std::string(158, 'y') + "DESCRIPTOR-END"};
        if (argc == 2 && std::string_view(argv[1]) == "completed-review") {
            const bool again = terminal.Revisit({"Signed transaction - review", {"Approved payment", lines.back()}, details});
            std::puts(again ? "SHOW QR AGAIN" : "FINISHED");
            return again ? 0 : 2;
        }
        const bool approved = terminal.Approve("Approval test", lines, "SIGN", details);
        std::puts(approved ? "APPROVED" : "DECLINED");
        return approved ? 0 : 2;
    } catch (const td::Cancelled&) { return 2; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 3; }
}
