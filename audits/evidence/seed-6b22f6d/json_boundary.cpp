// Public malformed reports placed at a guard page; no real wallet material.
#include "entropy_process.h"
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

int main()
{
    const rlimit zero{0,0}; setrlimit(RLIMIT_CORE, &zero);
    for (const auto* ending : {"-", "0", "n", "t", "f"}) for (int status : {0,1}) {
        for (bool sentinel : {false,true}) {
            const auto pid = fork();
            if (pid == 0) {
                const size_t page = sysconf(_SC_PAGESIZE);
                auto* memory = static_cast<unsigned char*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
                if (memory == MAP_FAILED || mprotect(memory + page, page, PROT_NONE) != 0) _exit(4);
                const std::string text = std::string("{\"IID\":false,\"errorLevel\":") + ending;
                auto* input = memory + page - text.size() - sentinel;
                std::memcpy(input, text.data(), text.size());
                if (sentinel) input[text.size()] = 0;
                try { (void)td::ParseAssessment({input,text.size()}, status); _exit(3); }
                catch (const std::exception&) { _exit(0); }
            }
            int result = 0;
            if (pid < 0 || waitpid(pid, &result, 0) != pid) return 2;
            std::printf("token=%s worker_exit=%d sentinel=%d signal=%d exit=%d\n", ending, status, sentinel,
                WIFSIGNALED(result) ? WTERMSIG(result) : 0, WIFEXITED(result) ? WEXITSTATUS(result) : -1);
        }
    }
}
