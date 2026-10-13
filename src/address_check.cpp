#include "address_check.h"
#include "terminal.h"
#include <key_io.h>
#include <algorithm>
#include <charconv>

namespace td {
CTxDestination ReadAddress(std::span<const uint8_t> text)
{
    Require(!text.empty() && text.size() <= MAX_ADDRESS_QR && std::all_of(text.begin(), text.end(), [](auto c) {
        return c >= 33 && c <= 126 && c != '#';
    }), "This isn't a supported Bitcoin address QR.");
    std::string_view address(reinterpret_cast<const char*>(text.data()), text.size());
    auto prefix = std::string(address.substr(0, 8));
    for (auto& c : prefix) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (prefix == "bitcoin:") {
        address.remove_prefix(8);
        const auto question = address.find('?');
        auto query = question == address.npos ? std::string_view{} : address.substr(question + 1);
        address = address.substr(0, question);
        // This checks the destination only. Optional amount/label/message values
        // are not interpreted or displayed. Unknown required parameters fail closed.
        while (!query.empty()) {
            const auto separator = query.find('&');
            const auto item = query.substr(0, separator);
            const auto equal = item.find('=');
            const auto name = item.substr(0, equal);
            Require(equal != item.npos && !name.empty() && name.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~") == name.npos, "Unsupported payment URI parameter.");
            Require(!name.starts_with("req-"), "This payment URI has unsupported required information.");
            if (separator == query.npos) break;
            query.remove_prefix(separator + 1);
        }
    }
    const auto destination = DecodeDestination(std::string(address));
    Require(IsValidDestination(destination), "This isn't a valid Bitcoin address for the selected network.");
    AddressPurpose(destination); // Reject future/unsupported witness types before searching.
    return destination;
}

unsigned AddressPurpose(const CTxDestination& destination)
{
    if (std::holds_alternative<PKHash>(destination)) return 44;
    if (std::holds_alternative<ScriptHash>(destination)) return 49;
    if (std::holds_alternative<WitnessV0KeyHash>(destination)) return 84;
    if (std::holds_alternative<WitnessV1Taproot>(destination)) return 86;
    Require(std::holds_alternative<WitnessV0ScriptHash>(destination), "This address type is not supported.");
    return 0;
}

std::optional<Position> FindAddress(const Policy& policy, const CScript& script, uint32_t& next,
    uint32_t end, const std::function<bool(uint32_t)>& proceed)
{
    Require(next <= end && end <= ADDRESS_INDEX_END && end - next <= ADDRESS_SEARCH_WINDOW && bool(proceed), "Invalid address search range");
    for (; next < end; ++next) for (unsigned branch : {0, 1}) {
        if (!proceed(next)) return {};
        if (policy.Script(branch, next) == script) return Position{branch, next};
    }
    return {};
}

std::optional<Position> Terminal::SearchAddress(const Policy& policy, const CScript& script,
    std::string_view wallet, uint32_t& next, uint32_t end)
{
    const auto view = View();
    auto lines = Wrap({std::string(wallet), "Receiving and change addresses"}, view.width);
    Require(lines.size() < view.height, "Not enough room for address search");
    lines.resize(view.height);
    lines.back() = "Checking address number:";
    Flush(); Draw(view, "Verifying address", lines, "Esc: Stop");
    auto painted = std::chrono::steady_clock::time_point::min();
    return FindAddress(policy, script, next, end, [&](uint32_t index) {
        Require(View() == view, "The screen changed. Start this check again.");
        const int key = Key(0);
        if (key == 27 || key == 3) return false;
        const auto now = std::chrono::steady_clock::now();
        if (painted == std::chrono::steady_clock::time_point::min() || now - painted >= std::chrono::milliseconds(100)) {
            const auto number = std::to_string(index);
            At(view.body + view.height - 1, view.left + view.width - 10);
            Write(std::string(10 - number.size(), ' ') + number);
            painted = now;
        }
        return true;
    });
}

namespace {
uint32_t Number(Terminal& terminal, const ReviewLines& lines, std::string_view prompt, uint32_t maximum, std::string_view help)
{
    std::string error;
    while (true) {
        const auto input = terminal.Input("Verify address", lines, prompt, std::to_string(maximum).size(), nullptr, {}, error, false, help);
        uint32_t number = 0;
        if (input.empty()) return number;
        const auto* first = reinterpret_cast<const char*>(input.data());
        const auto [end, status] = std::from_chars(first, first + input.size(), number);
        if (status == std::errc{} && end == first + input.size() && number <= maximum) return number;
        error = "Enter a number from 0 to " + std::to_string(maximum) + ".";
    }
}
}

void CheckAddress(Terminal& terminal, const Keys& keys, const ApprovedWallet* loaded, std::span<const uint8_t> text)
{
    const auto destination = ReadAddress(text);
    const auto script = GetScriptForDestination(destination);
    const auto purpose = AddressPurpose(destination);
    CTxDestination wallet_type;
    const bool compatible = loaded && ExtractDestination(loaded->policy.Script(0, 0), wallet_type) && wallet_type.index() == destination.index();
    if (compatible) Require(loaded->policy.Authorized(keys, loaded->proof), "The loaded wallet is not approved for this seed.");
    if (!purpose) {
        if (!compatible) {
            terminal.Confirm("Wallet setup needed", {"This address needs a wallet setup before it can be verified.", "",
                "Scan the matching wallet setup QR, then scan this address again."}, "go back");
            return;
        }
        if (!terminal.Confirm("Verify address", {"Check whether the scanned address belongs to your loaded multisig wallet.", "",
            "Wallet: " + loaded->policy.Name()}, "verify address")) return;
    }
    const auto choose = [&]() -> std::pair<Policy, std::string> {
        if (compatible && (!purpose || terminal.Menu("Which wallet should we verify against?",
            {"Loaded wallet: " + loaded->policy.Name(), "Single-signature account"}) == 0))
            return {loaded->policy, "Wallet: " + loaded->policy.Name()};
        auto type = AccountType(purpose); type = type.substr(0, type.find(" ("));
        const auto account = Number(terminal, {"Check whether the scanned address belongs to your wallet.", "", "Address type: " + type},
            "Account number [0]: ", 100, "Most wallets use account 0. Press Enter to use 0, or enter your wallet app's account number.");
        return {DefaultPolicy(keys, purpose, account), type + " - account " + std::to_string(account)};
    };
    auto [policy, wallet] = choose();
    uint32_t first = 0, next = 0, end = ADDRESS_SEARCH_WINDOW;
    while (true) {
        const auto match = terminal.SearchAddress(policy, script, wallet, next, end);
        if (match) {
            auto lines = AddressReview(policy, keys, *match, EncodeDestination(destination));
            lines.insert(lines.begin(), {"The scanned address belongs to this wallet.", ""});
            if (match->branch) lines.insert(lines.end(), {"", "Change addresses return the remaining funds from a transaction to the same wallet."});
            lines.insert(lines.end(), {"", "Before sending, compare this full address with the destination in your sending app."});
            terminal.Confirm("Address verified", lines, "finish", PolicyDetails(policy, keys));
            return;
        }
        const auto range = std::string(next - first == 1 ? "number " : "numbers ") + std::to_string(first)
            + (next - first > 1 ? "-" + std::to_string(next - 1) : "");
        const auto checked = next < end
            ? (next == first ? "No address numbers were fully checked." : "Checked receiving and change address " + range + ".")
                + std::string(" Resume to keep checking.")
            : "The scanned address did not match receiving or change address " + range
                + ". It may be outside this range or belong to another wallet or account.";
        while (true) {
            ReviewLines choices;
            int resume = -1, change = -1;
            if (next < ADDRESS_INDEX_END) { resume = choices.size(); choices.push_back(next < end ? "Resume checking" : "Search further"); }
            if (purpose) { change = choices.size(); choices.push_back(compatible ? "Choose another wallet" : "Try another account"); }
            choices.push_back("Check a specific address number");
            const auto title = next < end ? "Address check paused" : "No match found";
            const int action = terminal.Menu(title, choices, checked, true, wallet, true);
            try {
                if (action == resume) {
                    if (next == end) { first = next; end = std::min(ADDRESS_INDEX_END, next + ADDRESS_SEARCH_WINDOW); }
                } else if (action == change) {
                    auto replacement = choose(); policy = std::move(replacement.first); wallet = std::move(replacement.second);
                    first = next = 0; end = ADDRESS_SEARCH_WINDOW;
                } else {
                    first = next = Number(terminal, {"Check a specific receiving or change address.", "", wallet},
                        "Address number [0]: ", ADDRESS_INDEX_END - 1, "Address numbers start at 0. Find this number in your wallet app.");
                    end = next + 1;
                }
                break;
            } catch (const Cancelled&) {} // Cancelling a number/context edit keeps the scanned address and search position.
        }
    }
}
}
