#include "keys.h"
#include <crypto/sha256.h>
#include <cstdio>
#include <cstring>

static volatile unsigned char* last_hash;
static unsigned cleansed_hash;
extern "C" void __real__ZN7CSHA2568FinalizeEPh(CSHA256*, unsigned char*);
extern "C" void __wrap__ZN7CSHA2568FinalizeEPh(CSHA256* h, unsigned char* out) {
    __real__ZN7CSHA2568FinalizeEPh(h, out);
    last_hash = reinterpret_cast<volatile unsigned char*>(h);
}
extern "C" void __real__Z14memory_cleansePvm(void*, size_t);
extern "C" void __wrap__Z14memory_cleansePvm(void* p, size_t n) {
    if (p == const_cast<unsigned char*>(last_hash) && n >= sizeof(CSHA256)) ++cleansed_hash;
    __real__Z14memory_cleansePvm(p, n);
}
int main() {
    // All data in this process are public, reproducible fixtures.
    for (unsigned n : {16U, 32U}) {
        td::SecretBytes entropy(n);
        for (unsigned i = 0; i < n; ++i) entropy[i] = 0x40 + i;
        cleansed_hash = 0;
        const auto mnemonic = td::EntropyMnemonic(entropy);
        std::printf("ENCODE initial_hash_wipe_observed=%u\n", cleansed_hash);
        if (cleansed_hash != 1) return 2;
        cleansed_hash = 0;
        td::ValidateMnemonic(mnemonic);
        // Byte-by-byte forensic snapshot immediately after return, before any other call.
        unsigned char snapshot[sizeof(CSHA256)];
        for (unsigned i = 0; i < sizeof snapshot; ++i) snapshot[i] = last_hash[i];
        bool found = false;
        for (size_t pos = 0; pos + n <= sizeof snapshot; ++pos)
            if (std::memcmp(snapshot + pos, entropy.data(), n) == 0) found = true;
        std::printf("VALIDATE entropy_bytes=%u entire_entropy_in_returned_stack_context=%d hash_cleanse_calls=%u\n", n, found, cleansed_hash);
        if (!found || cleansed_hash) return 1;
    }
}
