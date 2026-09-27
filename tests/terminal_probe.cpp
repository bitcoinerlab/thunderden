#include "terminal.h"
#include <util/strencodings.h>
#include <unistd.h>
#include <cstdio>
#include <stdexcept>

int main(int argc, char** argv)
{
    try {
        td::Terminal terminal(dup(STDIN_FILENO));
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
        const bool approved = terminal.Approve("Approval test", lines, "SIGN");
        std::puts(approved ? "APPROVED" : "DECLINED");
        return approved ? 0 : 2;
    } catch (const td::Cancelled&) { return 2; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 3; }
}
