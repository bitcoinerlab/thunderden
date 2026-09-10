// Tests of Core's scalar checks and failure propagation.
#include <key.h>
#include <secp256k1.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

static constexpr std::array<unsigned char, 32> ORDER{
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,
    0xba,0xae,0xdc,0xe6,0xaf,0x48,0xa0,0x3b,0xbf,0xd2,0x5e,0x8c,0xd0,0x36,0x41,0x41};
static std::array<unsigned char, 32> injected_tweak;
static unsigned wrapped_calls;

extern "C" int __real_secp256k1_ec_seckey_tweak_add(
    const secp256k1_context*, unsigned char*, const unsigned char*);
extern "C" int __wrap_secp256k1_ec_seckey_tweak_add(
    const secp256k1_context* context, unsigned char* key, const unsigned char*)
{
    ++wrapped_calls;
    return __real_secp256k1_ec_seckey_tweak_add(context, key, injected_tweak.data());
}

static void Check(bool condition)
{
    if (!condition) throw std::runtime_error("Core key edge test failed");
}

int main()
{
    try {
        ECC_Context context;
        std::array<unsigned char, 32> scalar{};
        CKey key;
        key.Set(scalar.begin(), scalar.end(), true);
        Check(!key.IsValid());
        key.Set(ORDER.begin(), ORDER.end(), true);
        Check(!key.IsValid());
        scalar.back() = 1;
        key.Set(scalar.begin(), scalar.end(), true);
        Check(key.IsValid());

        ChainCode chain{}, child_chain;
        CKey child;
        for (const unsigned index : {0U, 0x80000000U}) {
            injected_tweak = ORDER; // IL >= n must fail, not be reduced modulo n.
            Check(!key.Derive(child, child_chain, index, chain));
            Check(!child.IsValid());
            --injected_tweak.back(); // kparent=1 and IL=n-1 produce invalid zero.
            Check(!key.Derive(child, child_chain, index, chain));
            Check(!child.IsValid());
            injected_tweak.fill(0); // IL=0 itself is valid for child derivation.
            Check(key.Derive(child, child_chain, index, chain));
            Check(child.IsValid() && std::memcmp(child.data(), key.data(), 32) == 0);
        }
        Check(wrapped_calls == 6);
        CExtKey max_depth{}, next{};
        max_depth.key = key;
        max_depth.nDepth = 255;
        Check(!max_depth.Derive(next, 0));
        std::puts("PASS: Core scalar/invalid-child/depth checks; zero child tweak is accepted");
        return 0;
    } catch (...) {
        std::fputs("Core key edge test failed\n", stderr);
        return 1;
    }
}
