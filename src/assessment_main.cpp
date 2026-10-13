#include "isolation.h"
#include "keys.h"
#include <fcntl.h>
#include <linux/magic.h>
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <omp.h>
#include <sys/stat.h>
#include <sys/vfs.h>

// The pinned official ea_non_iid entry point, compiled with a renamed main.
int NistMain(int argc, char* argv[]);
int main()
{
    try {
        td::LockProcess();
        struct stat input{}, output{};
        struct statfs in_fs{}, out_fs{};
        constexpr int seals = F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
        const int input_seals = fcntl(3, F_GET_SEALS);
        td::Require(fstat(3, &input) == 0 && fstat(4, &output) == 0 && S_ISREG(input.st_mode) && S_ISREG(output.st_mode)
            && input.st_size >= 1048576 && input.st_size <= 2097152 && output.st_size == 0
            && fstatfs(3, &in_fs) == 0 && fstatfs(4, &out_fs) == 0 && in_fs.f_type == TMPFS_MAGIC && out_fs.f_type == TMPFS_MAGIC
            && input_seals >= 0 && (input_seals & seals) == seals && fcntl(4, F_GET_SEALS) >= 0,
            "Invalid assessment descriptors");
        omp_set_dynamic(0); omp_set_num_threads(1);
        OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
        unsigned char warmup[32]; SHA256(reinterpret_cast<const unsigned char*>(""), 0, warmup);
        td::ConfineAssessment(); // No filesystem exceptions: anonymous memfds retain their descriptor rights.
        const char* args[]{"ea_non_iid", "-i", "-a", "-q", "-o", "/proc/self/fd/4", "/proc/self/fd/3", "8", nullptr};
        return NistMain(8, const_cast<char**>(args));
    } catch (...) { return 1; } // Never forward estimator text or sample metadata to the terminal.
}
