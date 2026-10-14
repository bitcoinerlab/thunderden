// Bounded resource-accounting check. Does not attempt OOM or use real inputs.
#include "isolation.h"
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>

int main()
{
    td::LockProcess(); td::ConfineScanner();
    const rlimit address_space{32UL * 1024 * 1024, 32UL * 1024 * 1024};
    if (setrlimit(RLIMIT_AS, &address_space) != 0) return 2;
    const int fd = memfd_create("public-budget-fixture", MFD_CLOEXEC);
    if (fd < 0) return 3;
    unsigned char block[4096]{};
    constexpr size_t count = 40UL * 1024 * 1024;
    for (size_t n = 0; n < count; n += sizeof(block))
        if (write(fd, block, sizeof(block)) != sizeof(block)) return 4;
    struct stat info{};
    if (fstat(fd, &info) != 0) return 5;
    close(fd);
    std::printf("address_space_limit=%lu memfd_bytes=%ld allocated_bytes=%ld\n",
        address_space.rlim_cur, info.st_size, info.st_blocks * 512);
    return info.st_size == count && info.st_blocks * 512 > static_cast<long>(address_space.rlim_cur) ? 0 : 1;
}
