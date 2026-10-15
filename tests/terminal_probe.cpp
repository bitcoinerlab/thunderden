#include "terminal.h"
#include "address_check.h"
#include "entropy.h"
#include "entropy_rng.h"
#include "multisig_fixture.h"
#include <key_io.h>
#include <util/strencodings.h>
#include <unistd.h>
#include <cstdio>
#include <stdexcept>

int main(int argc, char** argv)
{
    try {
        td::Terminal terminal(dup(STDIN_FILENO));
        if (argc == 2 && std::string_view(argv[1]).starts_with("entropy-")) {
            const std::string_view mode(argv[1]);
            if (mode == "entropy-generate" || mode == "entropy-rng-fail") {
                mock_entropy_rng = true; fail_entropy_rng = mode == "entropy-rng-fail";
                const auto mnemonic = td::NewMnemonic(terminal);
                td::ValidateMnemonic(mnemonic);
                td::Require(entropy_rng_calls == 1, "Generation did not request fresh randomness exactly once");
                std::puts("BACKUP VERIFIED");
            } else {
                const auto source = mode == "entropy-dice" ? td::ManualSource::Dice : td::ManualSource::Coins;
                const auto samples = terminal.ManualEntropy(source, 12);
                std::puts(HexStr(samples).c_str()); // Public test inputs only.
            }
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]).starts_with("address-")) {
            const std::string mode(argv[1]);
            ECC_Context context; SelectParams(ChainType::REGTEST); terminal.SetNetwork("Regtest");
            td::Keys keys(fixture::Bytes(fixture::WORDS), {}), other(fixture::Bytes(fixture::WORDS), fixture::Bytes("cosigner"));
            const unsigned purpose = mode == "address-ambiguous-single" ? 49 : 84;
            auto policy = td::DefaultPolicy(mode == "address-wrong" ? other : keys, purpose, mode == "address-account" ? 7 : 0);
            std::optional<td::ApprovedWallet> loaded;
            if (mode.find("multisig") != mode.npos || mode.find("ambiguous") != mode.npos || mode == "address-unloaded"
                || mode == "address-unapproved" || mode == "address-progress" || mode == "address-incompatible") {
                const unsigned kind = mode.find("ambiguous") != mode.npos || mode == "address-incompatible" ? 1 : 2;
                std::vector<const td::Keys*> signers{&keys, &other};
                std::vector<td::Path> paths{fixture::Path(kind), fixture::Path(kind, 7)};
                if (mode == "address-progress") {
                    signers.assign(20, &keys); paths.clear();
                    for (unsigned i = 0; i < 20; ++i) paths.push_back(fixture::Path(kind, i));
                }
                auto multi = td::ImportMultisig(fixture::Setup(kind, 2, signers, paths));
                if (mode != "address-ambiguous-single" && mode != "address-incompatible") policy = multi;
                if (mode != "address-unloaded") loaded.emplace(td::ApprovedWallet{multi,
                    mode == "address-unapproved" ? td::Digest{} : keys.RegistrationTag(multi.ID())});
            }
            const unsigned branch = mode.find("change") != mode.npos ? 1 : 0;
            const uint32_t index = mode == "address-late" ? 1007 : mode == "address-high" ? 0x7fffffff
                : mode == "address-progress" ? 1000000000 : 7;
            CTxDestination destination;
            td::Require(ExtractDestination(policy.Script(branch, index), destination), "No fixture address");
            const auto address = EncodeDestination(destination);
            std::printf("EXPECTED %s\n", address.c_str());
            const auto before = loaded ? std::optional{loaded->policy.ID()} : std::nullopt;
            td::CheckAddress(terminal, keys, loaded ? &*loaded : nullptr, fixture::Bytes("bitcoin:" + address + "?amount=0.1&label=Public%20test"));
            td::Require(!before || (loaded->policy.ID() == *before && keys.VerifyTag(*before, loaded->proof)), "Address check changed the loaded wallet");
            std::puts("CHECK FINISHED");
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]).starts_with("workflow-")) {
            const std::string mode = argv[1];
            ECC_Context context;
            SelectParams(ChainType::REGTEST);
            td::Keys alice(fixture::Bytes(fixture::WORDS), {}), bob(fixture::Bytes(fixture::WORDS), fixture::Bytes("cosigner"));
            const auto setup = mode.ends_with("-text")
                ? td::QRMessage{"bytes", td::CborBytes(fixture::Bytes(fixture::TextSetup(2, {&alice, &bob}, {fixture::Path(2), fixture::Path(2, 7)})))}
                : fixture::Setup(2, 2, {&alice, &bob}, {fixture::Path(2), fixture::Path(2, 7)});
            std::optional<td::ApprovedWallet> loaded;
            if (mode.starts_with("workflow-preload")) {
                td::Require(td::IsMultisigSetup(setup), "Main scanner missed wallet setup");
                if (!td::LoadWallet(terminal, alice, loaded, setup)) {
                    td::Require(!loaded, "Declined preload retained a wallet");
                    std::puts("EMPTY CANCELLED");
                    return 0;
                }
                terminal.Menu("What would you like to do?", {"Scan a QR code"}, {}, false,
                    "Loaded wallet: " + loaded->policy.Name());
            }
            if (mode.starts_with("workflow-replace")) {
                auto old = td::ImportMultisig(fixture::Setup(0, 2, {&alice, &bob}, {fixture::Path(0), fixture::Path(0)}));
                const auto id = old.ID();
                loaded.emplace(td::ApprovedWallet{std::move(old), alice.RegistrationTag(id)});
                if (mode == "workflow-replace-invalid-text") {
                    auto bad = setup; bad.cbor.pop_back();
                    try { td::LoadWallet(terminal, alice, loaded, bad); throw std::runtime_error("Invalid setup replaced wallet"); }
                    catch (const std::invalid_argument&) {}
                    td::Require(loaded->policy.ID() == id, "Invalid setup changed the loaded wallet");
                    std::puts("KEPT");
                    return 0;
                }
                const bool replaced = td::LoadWallet(terminal, alice, loaded, setup);
                td::Require((loaded->policy.ID() == id) == !replaced, "Cancelled setup replaced the loaded wallet");
                std::puts(replaced ? "REPLACED" : "KEPT");
                return 0;
            }
            auto first = td::DefaultPolicy(alice, 84, 0), second = td::DefaultPolicy(alice, 84, 1);
            auto multisig = td::ImportMultisig(setup);
            auto psbt = mode.starts_with("workflow-inline") || mode.starts_with("workflow-preload") || mode == "workflow-inferred"
                ? fixture::Spend({&multisig}, true)
                : fixture::Spend({&first, mode == "workflow-many" ? &second : &first}, mode == "workflow-fee" || mode == "workflow-change-fee");
            if (mode.starts_with("workflow-change")) {
                psbt.tx->vout[0].scriptPubKey = first.Script(1, 1000000000);
                psbt.outputs[0] = PSBTOutput{};
                UpdatePSBTOutput(first.PublicProvider({1, 1000000000}), psbt, 0);
            }
            if (mode == "workflow-inferred") fixture::AccountKeys(psbt, {&multisig});
            auto foreign = td::DefaultPolicy(bob, 84, 0);
            if (mode == "workflow-unmatched") psbt = fixture::Spend({&foreign});
            const auto bytes = fixture::Serialize(psbt);
            const td::QRMessage request{"crypto-psbt", td::CborBytes({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()})};
            std::optional<td::ReviewScreen> completed;
            unsigned scans = 0;
            std::optional<td::QRMessage> response;
            try {
                response = td::SignRequest(terminal, alice, loaded, request, [&] { ++scans; return setup; },
                    [&](const auto& review) {
                        if (mode.ends_with("-fee")) throw std::runtime_error("Unverified fee reached interactive approval");
                        if (mode.starts_with("workflow-change")) std::printf("CHANGE %s\n", review.outputs[0].address.c_str());
                        return td::ApproveTransaction(terminal, review, completed);
                    });
            } catch (const std::invalid_argument& error) {
                td::Require(mode.ends_with("-fee") && !completed && !response, "Unexpected workflow failure");
                // Match the main loop's error display; dismissal cannot retry signing.
                terminal.Notice("Could not complete this step", {"Check the details below before trying again.", "", error.what()});
                std::puts("BLOCKED");
                return 0;
            }
            td::Require(scans <= 1, "Setup caused an unnecessary transaction rescan");
            if (mode == "workflow-inferred" || mode == "workflow-unmatched" || mode.starts_with("workflow-preload"))
                td::Require(scans == 0, "Request made an unnecessary setup scan");
            if (response) {
                td::Require(response->type == "crypto-psbt", "Signed reply lost the standard PSBT format");
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
