#include "keys.h"
#include <crypto/sha256.h>
#include <algorithm>
#include <cstdio>
#include <numeric>

namespace {
struct InjectedFailure {};
CSHA256* pending = nullptr;
unsigned finalized = 0, cleansed = 0, fail_at = 0;
bool missed = false, erased = true;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Reset(unsigned failure = 0)
{
    pending = nullptr; finalized = cleansed = 0; fail_at = failure;
    missed = false; erased = true;
}
void CheckWipes(unsigned expected)
{
    Check(finalized == expected && cleansed == expected && !pending && !missed && erased,
        "Mnemonic hash context was not fully cleansed");
}
}

// Observe the real hash and cleanse only while the context is still alive.
// These wrappers are linked into this test executable, never the signer.
extern "C" void __real__ZN7CSHA2568FinalizeEPh(CSHA256*, unsigned char*);
extern "C" void __wrap__ZN7CSHA2568FinalizeEPh(CSHA256* hash, unsigned char* out)
{
    missed |= pending != nullptr;
    pending = hash;
    ++finalized;
    __real__ZN7CSHA2568FinalizeEPh(hash, out);
    if (finalized == fail_at) throw InjectedFailure{};
}
extern "C" void __real__Z14memory_cleansePvm(void*, size_t);
extern "C" void __wrap__Z14memory_cleansePvm(void* ptr, size_t size)
{
    const bool context = ptr == pending && size >= sizeof(CSHA256);
    __real__Z14memory_cleansePvm(ptr, size);
    if (context) {
        const auto* bytes = static_cast<const unsigned char*>(ptr);
        erased &= std::all_of(bytes, bytes + sizeof(CSHA256), [](auto b) { return b == 0; });
        ++cleansed;
        pending = nullptr;
    }
}

int main()
{
    try {
        // Public zero-entropy vectors cover all supported import lengths.
        unsigned count = 12;
        for (const auto* last : {"about", "address", "agent", "admit", "art"}) {
            std::string prefix;
            for (unsigned i = 1; i < count; ++i) prefix += "abandon ";
            const auto text = prefix + last;
            const td::SecretBytes words(text.begin(), text.end());
            Reset(); td::ValidateMnemonic(words); CheckWipes(1);
            const auto invalid = prefix + "abandon";
            Reset();
            bool rejected = false;
            try { td::ValidateMnemonic(td::SecretBytes(invalid.begin(), invalid.end())); }
            catch (const std::invalid_argument&) { rejected = true; }
            Check(rejected, "Invalid checksum accepted"); CheckWipes(1);
            Reset(1);
            bool injected = false;
            try { td::ValidateMnemonic(words); } catch (const InjectedFailure&) { injected = true; }
            Check(injected, "Hash exception was not injected"); CheckWipes(1);
            count += 3;
        }
        for (unsigned size : {16, 32}) {
            td::SecretBytes entropy(size); std::iota(entropy.begin(), entropy.end(), 0x40);
            Reset(); (void)td::EntropyMnemonic(entropy); CheckWipes(2);
            for (unsigned failure : {1, 2}) {
                Reset(failure);
                bool injected = false;
                try { (void)td::EntropyMnemonic(entropy); } catch (const InjectedFailure&) { injected = true; }
                Check(injected, "Generation hash exception was not injected"); CheckWipes(failure);
            }
        }
        std::puts("PASS: mnemonic hash contexts erased on success, invalid checksum and hash exceptions");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
