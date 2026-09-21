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
            terminal.Screen("Secret input test", {"Enter public test data"});
            const auto secret = terminal.Input("Secret: ", 32, true);
            std::puts(HexStr(secret).c_str()); // Test-only public fixture output.
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "mnemonic") {
            const auto mnemonic = terminal.Mnemonic();
            std::puts(HexStr(mnemonic).c_str()); // Test-only public fixture output.
            return 0;
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
