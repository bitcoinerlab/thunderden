// Test-only byte interface to unmodified Bitcoin Core BIP32/key code.
// stdin: uint32be seed length + seed; uint32be path byte length + uint32be indices.
// stdout: root fingerprint (4), mainnet public extended key (78), private key (78).
// This probe accepts binary test seeds, not BIP39 mnemonics or passphrases.
#include <crypto/common.h>
#include <key.h>
#include <support/allocators/secure.h>

#include <algorithm>
#include <cstdio>
#include <span>
#include <vector>

using SecretBytes = std::vector<unsigned char, secure_allocator<unsigned char>>;

static bool ReadField(SecretBytes& bytes, size_t limit)
{
    unsigned char header[4];
    if (std::fread(header, 1, 4, stdin) != 4) return false;
    const uint32_t length = ReadBE32(header);
    if (length > limit) return false;
    bytes.resize(length);
    return length == 0 || std::fread(bytes.data(), 1, length, stdin) == length;
}

static bool Derive()
{
    SecretBytes seed, path, result(160);
    if (!ReadField(seed, 64) || seed.size() < 16) return false;
    if (!ReadField(path, 255 * 4) || path.size() % 4 != 0) return false;
    if (std::fgetc(stdin) != EOF || std::ferror(stdin)) return false;

    ECC_Context context;
    CExtKey key;
    key.SetSeed(std::as_bytes(std::span{seed}));
    if (!key.key.IsValid()) return false;
    const auto fingerprint = key.key.GetPubKey().GetID();
    std::copy_n(fingerprint.begin(), 4, result.begin());

    for (size_t offset = 0; offset < path.size(); offset += 4) {
        CExtKey child;
        if (!key.Derive(child, ReadBE32(path.data() + offset))) return false;
        key = child;
    }
    WriteBE32(result.data() + 4, 0x0488B21E);
    key.Neuter().Encode(result.data() + 8);
    WriteBE32(result.data() + 82, 0x0488ADE4);
    key.Encode(result.data() + 86);
    return std::fwrite(result.data(), 1, result.size(), stdout) == result.size()
        && std::fflush(stdout) == 0;
}

int main(int argc, char**)
{
    std::setvbuf(stdin, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        if (argc == 1 && Derive()) return 0;
    } catch (...) {
        // Core exception text may contain input data; never print it here.
    }
    std::fputs("Invalid input or key derivation failed\n", stderr);
    return 1;
}
