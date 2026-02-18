#define _POSIX_C_SOURCE 200809L

#include "psbt_payload_decoder.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zlib.h>

#define MAX_PSBT_BYTES (1024U * 1024U)
#define MAX_MULTIPART_PARTS 4096U
#define MAX_TEXT_FRAGMENT 131072U
#define MAX_UR_TYPE_LEN 31U
#define MULTIPART_STALE_SECONDS 20

struct bitset {
    size_t nbits;
    size_t nwords;
    uint64_t *words;
    size_t count;
};

struct fountain_part {
    struct bitset indexes;
    uint8_t *data;
    size_t data_len;
};

struct part_vec {
    struct fountain_part **items;
    size_t count;
    size_t cap;
};

struct fountain_decoder {
    int initialized;
    int complete;
    int success;

    uint32_t expected_seq_len;
    uint32_t expected_message_len;
    uint32_t expected_fragment_len;
    uint32_t expected_checksum;

    struct bitset received_indexes;
    struct fountain_part **simple_parts;
    struct part_vec mixed_parts;
    struct part_vec queue;

    uint8_t *result;
    size_t result_len;
};

struct ur_state {
    int active;
    char type[MAX_UR_TYPE_LEN + 1U];
    time_t last_update;
    struct fountain_decoder fountain;
};

struct bbqr_state {
    int active;
    char encoding;
    char type;
    size_t total_parts;
    size_t received_parts;
    char **parts;
    size_t *part_lens;
    time_t last_update;
};

struct pofn_state {
    int active;
    size_t total_parts;
    size_t received_parts;
    char **parts;
    size_t *part_lens;
    time_t last_update;
};

struct psbt_payload_decoder {
    struct ur_state ur;
    struct bbqr_state bbqr;
    struct pofn_state pofn;
};

struct ur_fountain_part {
    uint32_t seq_num;
    uint32_t seq_len;
    uint32_t message_len;
    uint32_t checksum;
    uint8_t *data;
    size_t data_len;
};

struct xoshiro256ss {
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;
};

struct sha256_ctx {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

static int safe_mul_size(size_t a, size_t b, size_t *out)
{
    if (a != 0 && b > SIZE_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static int safe_add_size(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int is_psbt_magic(const uint8_t *data, size_t data_len)
{
    static const uint8_t magic[] = {'p', 's', 'b', 't', 0xff};

    return data_len >= sizeof(magic) && memcmp(data, magic, sizeof(magic)) == 0;
}

static void trim_ascii_ws(const char *in, size_t in_len, const char **out, size_t *out_len)
{
    size_t start = 0;
    size_t end = in_len;

    while (start < in_len && isspace((unsigned char)in[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)in[end - 1U])) {
        end--;
    }

    *out = in + start;
    *out_len = end - start;
}

static int payload_is_text(const uint8_t *payload, size_t payload_len)
{
    size_t i;

    for (i = 0; i < payload_len; i++) {
        unsigned char c = payload[i];
        if (c == '\0') {
            return 0;
        }
        if (isprint(c) || isspace(c)) {
            continue;
        }
        return 0;
    }
    return 1;
}

static int lower_char(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static int starts_with_ci(const char *s, size_t s_len, const char *prefix)
{
    size_t i;
    size_t p_len = strlen(prefix);

    if (s_len < p_len) {
        return 0;
    }

    for (i = 0; i < p_len; i++) {
        if (lower_char((unsigned char)s[i]) != lower_char((unsigned char)prefix[i])) {
            return 0;
        }
    }
    return 1;
}

static int base64_value(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+' || ch == '-') {
        return 62;
    }
    if (ch == '/' || ch == '_') {
        return 63;
    }
    return -1;
}

static int looks_like_base64(const char *text, size_t text_len)
{
    size_t i;
    size_t data_chars = 0;

    for (i = 0; i < text_len; i++) {
        int ch = (unsigned char)text[i];

        if (isspace(ch) || ch == '=') {
            continue;
        }
        if (base64_value(ch) < 0) {
            return 0;
        }
        data_chars++;
    }

    return data_chars >= 8;
}

static int base64_decode_alloc(const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    uint8_t *buf = NULL;
    size_t cap = ((text_len / 4U) + 2U) * 3U;
    size_t pos = 0;
    unsigned int acc = 0;
    int bits = 0;
    int seen_pad = 0;
    int saw_data = 0;
    size_t i;

    *out = NULL;
    *out_len = 0;

    if (cap == 0) {
        cap = 8;
    }

    buf = malloc(cap);
    if (buf == NULL) {
        return 0;
    }

    for (i = 0; i < text_len; i++) {
        int ch = (unsigned char)text[i];
        int v;

        if (isspace(ch)) {
            continue;
        }
        if (ch == '=') {
            seen_pad = 1;
            continue;
        }

        v = base64_value(ch);
        if (v < 0) {
            free(buf);
            return 0;
        }
        if (seen_pad) {
            free(buf);
            return 0;
        }

        saw_data = 1;
        acc = (acc << 6U) | (unsigned int)v;
        bits += 6;

        while (bits >= 8) {
            bits -= 8;
            if (pos >= cap) {
                free(buf);
                return 0;
            }
            buf[pos++] = (uint8_t)((acc >> bits) & 0xffU);
        }
    }

    if (!saw_data || pos == 0 || pos > MAX_PSBT_BYTES) {
        free(buf);
        return 0;
    }

    *out = buf;
    *out_len = pos;
    return 1;
}

static int base64_encode(const uint8_t *data, size_t data_len, char *out, size_t out_len)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((data_len + 2U) / 3U) * 4U + 1U;
    size_t i;
    size_t o = 0;

    if (out_len < need) {
        return 0;
    }

    for (i = 0; i + 3U <= data_len; i += 3U) {
        uint32_t v = ((uint32_t)data[i] << 16U) | ((uint32_t)data[i + 1U] << 8U) | (uint32_t)data[i + 2U];
        out[o++] = table[(v >> 18U) & 0x3fU];
        out[o++] = table[(v >> 12U) & 0x3fU];
        out[o++] = table[(v >> 6U) & 0x3fU];
        out[o++] = table[v & 0x3fU];
    }

    if (i < data_len) {
        uint32_t v = (uint32_t)data[i] << 16U;
        out[o++] = table[(v >> 18U) & 0x3fU];
        if (i + 1U < data_len) {
            v |= (uint32_t)data[i + 1U] << 8U;
            out[o++] = table[(v >> 12U) & 0x3fU];
            out[o++] = table[(v >> 6U) & 0x3fU];
            out[o++] = '=';
        } else {
            out[o++] = table[(v >> 12U) & 0x3fU];
            out[o++] = '=';
            out[o++] = '=';
        }
    }

    out[o] = '\0';
    return 1;
}

static int hex_value(int ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int looks_like_hex(const char *text, size_t text_len)
{
    size_t i;

    if (text_len == 0 || (text_len % 2U) != 0U) {
        return 0;
    }

    for (i = 0; i < text_len; i++) {
        if (hex_value((unsigned char)text[i]) < 0) {
            return 0;
        }
    }

    return 1;
}

static int hex_decode_alloc(const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    uint8_t *buf;
    size_t i;
    size_t n;

    *out = NULL;
    *out_len = 0;

    if (!looks_like_hex(text, text_len)) {
        return 0;
    }

    n = text_len / 2U;
    if (n == 0 || n > MAX_PSBT_BYTES) {
        return 0;
    }

    buf = malloc(n);
    if (buf == NULL) {
        return 0;
    }

    for (i = 0; i < n; i++) {
        int hi = hex_value((unsigned char)text[2U * i]);
        int lo = hex_value((unsigned char)text[2U * i + 1U]);
        if (hi < 0 || lo < 0) {
            free(buf);
            return 0;
        }
        buf[i] = (uint8_t)((hi << 4) | lo);
    }

    *out = buf;
    *out_len = n;
    return 1;
}

static int base43_value(int ch)
{
    static const char *alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:";
    const char *p = strchr(alphabet, ch);

    if (p == NULL) {
        return -1;
    }
    return (int)(p - alphabet);
}

static int looks_like_base43(const char *text, size_t text_len)
{
    size_t i;

    if (text_len == 0) {
        return 0;
    }

    for (i = 0; i < text_len; i++) {
        if (base43_value((unsigned char)text[i]) < 0) {
            return 0;
        }
    }
    return 1;
}

static int base43_decode_alloc(const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    uint8_t *tmp = NULL;
    size_t tmp_len = 0;
    size_t tmp_cap = 0;
    size_t leading_zeroes = 0;
    size_t i;
    uint8_t *buf = NULL;
    size_t final_len;

    *out = NULL;
    *out_len = 0;

    if (!looks_like_base43(text, text_len)) {
        return 0;
    }

    while (leading_zeroes < text_len && text[leading_zeroes] == '0') {
        leading_zeroes++;
    }

    for (i = leading_zeroes; i < text_len; i++) {
        int carry = base43_value((unsigned char)text[i]);
        size_t j;

        if (carry < 0) {
            free(tmp);
            return 0;
        }

        for (j = 0; j < tmp_len; j++) {
            int x = (int)tmp[j] * 43 + carry;
            tmp[j] = (uint8_t)(x & 0xff);
            carry = x >> 8;
        }

        while (carry > 0) {
            uint8_t *grown;
            size_t new_cap;

            if (tmp_len == tmp_cap) {
                new_cap = (tmp_cap == 0) ? 16U : tmp_cap * 2U;
                if (new_cap > MAX_PSBT_BYTES) {
                    free(tmp);
                    return 0;
                }
                grown = realloc(tmp, new_cap);
                if (grown == NULL) {
                    free(tmp);
                    return 0;
                }
                tmp = grown;
                tmp_cap = new_cap;
            }

            tmp[tmp_len++] = (uint8_t)(carry & 0xff);
            carry >>= 8;
        }
    }

    if (!safe_add_size(leading_zeroes, tmp_len, &final_len) || final_len == 0 || final_len > MAX_PSBT_BYTES) {
        free(tmp);
        return 0;
    }

    buf = malloc(final_len);
    if (buf == NULL) {
        free(tmp);
        return 0;
    }

    memset(buf, 0, leading_zeroes);
    for (i = 0; i < tmp_len; i++) {
        buf[final_len - 1U - i] = tmp[i];
    }

    free(tmp);
    *out = buf;
    *out_len = final_len;
    return 1;
}

static int base32_value(int ch)
{
    ch = toupper((unsigned char)ch);
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= '2' && ch <= '7') {
        return ch - '2' + 26;
    }
    return -1;
}

static int base32_decode_alloc(const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    uint8_t *buf;
    size_t cap = (text_len * 5U) / 8U + 8U;
    size_t pos = 0;
    uint32_t acc = 0;
    int bits = 0;
    int saw = 0;
    int seen_pad = 0;
    size_t i;

    *out = NULL;
    *out_len = 0;

    if (cap == 0 || cap > MAX_PSBT_BYTES) {
        cap = MAX_PSBT_BYTES;
    }

    buf = malloc(cap);
    if (buf == NULL) {
        return 0;
    }

    for (i = 0; i < text_len; i++) {
        int ch = (unsigned char)text[i];
        int v;

        if (isspace(ch)) {
            continue;
        }
        if (ch == '=') {
            seen_pad = 1;
            continue;
        }

        v = base32_value(ch);
        if (v < 0 || seen_pad) {
            free(buf);
            return 0;
        }

        saw = 1;
        acc = (acc << 5U) | (uint32_t)v;
        bits += 5;

        while (bits >= 8) {
            bits -= 8;
            if (pos >= cap) {
                free(buf);
                return 0;
            }
            buf[pos++] = (uint8_t)((acc >> bits) & 0xffU);
        }
    }

    if (!saw || pos == 0 || pos > MAX_PSBT_BYTES) {
        free(buf);
        return 0;
    }

    *out = buf;
    *out_len = pos;
    return 1;
}

static int inflate_raw_alloc(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len)
{
    z_stream zs;
    uint8_t *buf = NULL;
    size_t cap;
    int rc;

    *out = NULL;
    *out_len = 0;

    if (in_len == 0) {
        return 0;
    }

    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return 0;
    }

    cap = in_len * 4U + 1024U;
    if (cap < 4096U) {
        cap = 4096U;
    }
    if (cap > MAX_PSBT_BYTES) {
        cap = MAX_PSBT_BYTES;
    }

    buf = malloc(cap);
    if (buf == NULL) {
        inflateEnd(&zs);
        return 0;
    }

    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)in_len;

    while (1) {
        if (zs.total_out >= cap) {
            uint8_t *grown;
            size_t new_cap;

            if (cap >= MAX_PSBT_BYTES) {
                free(buf);
                inflateEnd(&zs);
                return 0;
            }

            new_cap = cap * 2U;
            if (new_cap > MAX_PSBT_BYTES) {
                new_cap = MAX_PSBT_BYTES;
            }

            grown = realloc(buf, new_cap);
            if (grown == NULL) {
                free(buf);
                inflateEnd(&zs);
                return 0;
            }

            buf = grown;
            cap = new_cap;
        }

        zs.next_out = buf + zs.total_out;
        zs.avail_out = (uInt)(cap - zs.total_out);

        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) {
            break;
        }
        if (rc != Z_OK) {
            free(buf);
            inflateEnd(&zs);
            return 0;
        }
    }

    if (zs.total_out == 0 || zs.total_out > MAX_PSBT_BYTES) {
        free(buf);
        inflateEnd(&zs);
        return 0;
    }

    *out = buf;
    *out_len = zs.total_out;
    inflateEnd(&zs);
    return 1;
}

static const char *bytewords_words =
    "ableacidalsoapexaquaarchatomauntawayaxisbackbaldbarnbeltbetabiasbluebodybragbrewbulbbuzz"
    "calmcashcatschefcityclawcodecolacookcostcruxcurlcuspcyandarkdatadaysdelidicedietdoordown"
    "drawdropdrumdulldutyeacheasyechoedgeepicevenexamexiteyesfactfairfernfigsfilmfishfizzflap"
    "flewfluxfoxyfreefrogfuelfundgalagamegeargemsgiftgirlglowgoodgraygrimgurugushgyrohalfhang"
    "hardhawkheathelphighhillholyhopehornhutsicedideaidleinchinkyintoirisironitemjadejazzjoin"
    "joltjowljudojugsjumpjunkjurykeepkenokeptkeyskickkilnkingkitekiwiknoblamblavalazyleaflegs"
    "liarlimplionlistlogoloudloveluaulucklungmainmanymathmazememomenumeowmildmintmissmonknail"
    "navyneednewsnextnoonnotenumbobeyoboeomitonyxopenovalowlspaidpartpeckplaypluspoempoolpose"
    "puffpumapurrquadquizraceramprealredorichroadrockroofrubyruinrunsrustsafesagascarsetssilk"
    "skewslotsoapsolosongstubsurfswantacotasktaxitenttiedtimetinytoiltombtoystriptunatwinugly"
    "undouniturgeuservastveryvetovialvibeviewvisavoidvowswallwandwarmwaspwavewaxywebswhatwhen"
    "whizwolfworkyankyawnyellyogayurtzapszerozestzinczonezoom";

static int bytewords_lookup_pair(int a, int b)
{
    size_t i;

    for (i = 0; i < 256U; i++) {
        const char *w = bytewords_words + 4U * i;
        if (w[0] == (char)a && w[3] == (char)b) {
            return (int)i;
        }
    }

    return -1;
}

static int bytewords_minimal_decode_alloc(const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    uint8_t *raw = NULL;
    uint8_t *buf = NULL;
    size_t raw_len;
    size_t data_len;
    size_t i;
    uint32_t crc;

    *out = NULL;
    *out_len = 0;

    if (text_len < 10U || (text_len % 2U) != 0U) {
        return 0;
    }

    raw_len = text_len / 2U;
    raw = malloc(raw_len);
    if (raw == NULL) {
        return 0;
    }

    for (i = 0; i < raw_len; i++) {
        int a = lower_char((unsigned char)text[2U * i]);
        int b = lower_char((unsigned char)text[2U * i + 1U]);
        int v = bytewords_lookup_pair(a, b);

        if (v < 0) {
            free(raw);
            return 0;
        }
        raw[i] = (uint8_t)v;
    }

    if (raw_len <= 4U) {
        free(raw);
        return 0;
    }

    data_len = raw_len - 4U;
    crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, raw, (uInt)data_len);

    if (raw[data_len + 0U] != (uint8_t)((crc >> 24U) & 0xffU) ||
        raw[data_len + 1U] != (uint8_t)((crc >> 16U) & 0xffU) ||
        raw[data_len + 2U] != (uint8_t)((crc >> 8U) & 0xffU) ||
        raw[data_len + 3U] != (uint8_t)(crc & 0xffU)) {
        free(raw);
        return 0;
    }

    if (data_len == 0 || data_len > MAX_PSBT_BYTES) {
        free(raw);
        return 0;
    }

    buf = malloc(data_len);
    if (buf == NULL) {
        free(raw);
        return 0;
    }

    memcpy(buf, raw, data_len);
    free(raw);

    *out = buf;
    *out_len = data_len;
    return 1;
}

static int cbor_read_uint(const uint8_t *buf, size_t len, size_t *off, uint8_t *major, uint64_t *value)
{
    uint8_t ib;
    uint8_t ai;

    if (*off >= len) {
        return 0;
    }

    ib = buf[*off];
    (*off)++;
    *major = (uint8_t)(ib >> 5U);
    ai = (uint8_t)(ib & 0x1fU);

    if (ai < 24U) {
        *value = ai;
        return 1;
    }
    if (ai == 24U) {
        if (*off + 1U > len) {
            return 0;
        }
        *value = buf[*off];
        *off += 1U;
        return 1;
    }
    if (ai == 25U) {
        if (*off + 2U > len) {
            return 0;
        }
        *value = ((uint64_t)buf[*off] << 8U) | (uint64_t)buf[*off + 1U];
        *off += 2U;
        return 1;
    }
    if (ai == 26U) {
        if (*off + 4U > len) {
            return 0;
        }
        *value = ((uint64_t)buf[*off] << 24U) |
                 ((uint64_t)buf[*off + 1U] << 16U) |
                 ((uint64_t)buf[*off + 2U] << 8U) |
                 (uint64_t)buf[*off + 3U];
        *off += 4U;
        return 1;
    }
    if (ai == 27U) {
        if (*off + 8U > len) {
            return 0;
        }
        *value = ((uint64_t)buf[*off] << 56U) |
                 ((uint64_t)buf[*off + 1U] << 48U) |
                 ((uint64_t)buf[*off + 2U] << 40U) |
                 ((uint64_t)buf[*off + 3U] << 32U) |
                 ((uint64_t)buf[*off + 4U] << 24U) |
                 ((uint64_t)buf[*off + 5U] << 16U) |
                 ((uint64_t)buf[*off + 6U] << 8U) |
                 (uint64_t)buf[*off + 7U];
        *off += 8U;
        return 1;
    }

    return 0;
}

static int cbor_extract_single_bstr(const uint8_t *buf, size_t len, uint8_t **out, size_t *out_len)
{
    uint8_t major;
    uint64_t n;
    size_t off = 0;
    uint8_t *copy;

    *out = NULL;
    *out_len = 0;

    if (!cbor_read_uint(buf, len, &off, &major, &n) || major != 2U) {
        return 0;
    }
    if (n == 0 || n > MAX_PSBT_BYTES) {
        return 0;
    }
    if (n > len - off) {
        return 0;
    }
    if (off + (size_t)n != len) {
        return 0;
    }

    copy = malloc((size_t)n);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, buf + off, (size_t)n);

    *out = copy;
    *out_len = (size_t)n;
    return 1;
}

static int cbor_parse_fountain_part(const uint8_t *buf, size_t len, struct ur_fountain_part *part)
{
    uint8_t major;
    uint64_t n;
    size_t off = 0;

    memset(part, 0, sizeof(*part));

    if (!cbor_read_uint(buf, len, &off, &major, &n) || major != 4U || n != 5U) {
        return 0;
    }

    if (!cbor_read_uint(buf, len, &off, &major, &n) || major != 0U || n == 0U || n > UINT32_MAX) {
        return 0;
    }
    part->seq_num = (uint32_t)n;

    if (!cbor_read_uint(buf, len, &off, &major, &n) || major != 0U || n == 0U || n > UINT32_MAX) {
        return 0;
    }
    part->seq_len = (uint32_t)n;

    if (!cbor_read_uint(buf, len, &off, &major, &n) || major != 0U || n == 0U || n > UINT32_MAX) {
        return 0;
    }
    part->message_len = (uint32_t)n;

    if (!cbor_read_uint(buf, len, &off, &major, &n) || major != 0U || n > UINT32_MAX) {
        return 0;
    }
    part->checksum = (uint32_t)n;

    if (!cbor_read_uint(buf, len, &off, &major, &n) || major != 2U || n == 0U || n > MAX_PSBT_BYTES) {
        return 0;
    }
    if (n > len - off) {
        return 0;
    }

    part->data = malloc((size_t)n);
    if (part->data == NULL) {
        return 0;
    }
    memcpy(part->data, buf + off, (size_t)n);
    part->data_len = (size_t)n;
    off += (size_t)n;

    if (off != len) {
        free(part->data);
        memset(part, 0, sizeof(*part));
        return 0;
    }

    return 1;
}

static void ur_fountain_part_free(struct ur_fountain_part *part)
{
    if (part->data != NULL) {
        free(part->data);
    }
    memset(part, 0, sizeof(*part));
}

static int bitset_init(struct bitset *set, size_t nbits)
{
    size_t nwords = (nbits + 63U) / 64U;

    memset(set, 0, sizeof(*set));
    set->nbits = nbits;
    set->nwords = nwords;

    if (nwords == 0) {
        return 1;
    }

    set->words = calloc(nwords, sizeof(uint64_t));
    if (set->words == NULL) {
        memset(set, 0, sizeof(*set));
        return 0;
    }

    return 1;
}

static void bitset_free(struct bitset *set)
{
    free(set->words);
    memset(set, 0, sizeof(*set));
}

static int bitset_clone(struct bitset *dst, const struct bitset *src)
{
    if (!bitset_init(dst, src->nbits)) {
        return 0;
    }

    if (src->nwords > 0U) {
        memcpy(dst->words, src->words, src->nwords * sizeof(uint64_t));
    }
    dst->count = src->count;
    return 1;
}

static int bitset_set(struct bitset *set, size_t index)
{
    size_t w;
    uint64_t mask;

    if (index >= set->nbits) {
        return 0;
    }

    w = index / 64U;
    mask = (uint64_t)1U << (index % 64U);

    if ((set->words[w] & mask) == 0U) {
        set->words[w] |= mask;
        set->count++;
    }

    return 1;
}

static int bitset_test(const struct bitset *set, size_t index)
{
    size_t w;
    uint64_t mask;

    if (index >= set->nbits) {
        return 0;
    }

    w = index / 64U;
    mask = (uint64_t)1U << (index % 64U);
    return (set->words[w] & mask) != 0U;
}

static int bitset_equals(const struct bitset *a, const struct bitset *b)
{
    if (a->nbits != b->nbits || a->nwords != b->nwords || a->count != b->count) {
        return 0;
    }
    if (a->nwords == 0U) {
        return 1;
    }
    return memcmp(a->words, b->words, a->nwords * sizeof(uint64_t)) == 0;
}

static int bitset_contains_all(const struct bitset *a, const struct bitset *b)
{
    size_t i;

    if (a->nbits != b->nbits || a->nwords != b->nwords) {
        return 0;
    }

    for (i = 0; i < a->nwords; i++) {
        if ((a->words[i] & b->words[i]) != b->words[i]) {
            return 0;
        }
    }
    return 1;
}

static int bitset_subtract(struct bitset *out, const struct bitset *a, const struct bitset *b)
{
    size_t i;

    if (a->nbits != b->nbits || a->nwords != b->nwords) {
        return 0;
    }
    if (!bitset_init(out, a->nbits)) {
        return 0;
    }

    for (i = 0; i < a->nwords; i++) {
        uint64_t w = a->words[i] & ~b->words[i];
        out->words[i] = w;
        out->count += (size_t)__builtin_popcountll(w);
    }

    return 1;
}

static size_t bitset_first_index(const struct bitset *set)
{
    size_t i;

    for (i = 0; i < set->nwords; i++) {
        uint64_t w = set->words[i];
        if (w != 0U) {
            return i * 64U + (size_t)__builtin_ctzll(w);
        }
    }

    return SIZE_MAX;
}

static struct fountain_part *fountain_part_new_owned(struct bitset *indexes, uint8_t *data, size_t data_len)
{
    struct fountain_part *part;

    part = calloc(1, sizeof(*part));
    if (part == NULL) {
        bitset_free(indexes);
        free(data);
        return NULL;
    }

    part->indexes = *indexes;
    part->data = data;
    part->data_len = data_len;
    memset(indexes, 0, sizeof(*indexes));

    return part;
}

static struct fountain_part *fountain_part_clone(const struct fountain_part *src)
{
    struct bitset indexes;
    uint8_t *data;

    memset(&indexes, 0, sizeof(indexes));

    if (!bitset_clone(&indexes, &src->indexes)) {
        return NULL;
    }

    data = malloc(src->data_len);
    if (data == NULL) {
        bitset_free(&indexes);
        return NULL;
    }
    memcpy(data, src->data, src->data_len);

    return fountain_part_new_owned(&indexes, data, src->data_len);
}

static void fountain_part_free(struct fountain_part *part)
{
    if (part == NULL) {
        return;
    }
    bitset_free(&part->indexes);
    free(part->data);
    free(part);
}

static int part_vec_push(struct part_vec *vec, struct fountain_part *part)
{
    if (vec->count == vec->cap) {
        size_t new_cap = (vec->cap == 0U) ? 16U : vec->cap * 2U;
        struct fountain_part **grown = realloc(vec->items, new_cap * sizeof(*grown));
        if (grown == NULL) {
            return 0;
        }
        vec->items = grown;
        vec->cap = new_cap;
    }

    vec->items[vec->count++] = part;
    return 1;
}

static struct fountain_part *part_vec_pop_front(struct part_vec *vec)
{
    struct fountain_part *part;

    if (vec->count == 0U) {
        return NULL;
    }

    part = vec->items[0];
    if (vec->count > 1U) {
        memmove(vec->items, vec->items + 1U, (vec->count - 1U) * sizeof(*vec->items));
    }
    vec->count--;

    return part;
}

static void part_vec_clear(struct part_vec *vec)
{
    size_t i;

    for (i = 0; i < vec->count; i++) {
        fountain_part_free(vec->items[i]);
    }

    free(vec->items);
    memset(vec, 0, sizeof(*vec));
}

static int part_vec_contains_indexes(const struct part_vec *vec, const struct bitset *indexes)
{
    size_t i;

    for (i = 0; i < vec->count; i++) {
        if (bitset_equals(&vec->items[i]->indexes, indexes)) {
            return 1;
        }
    }
    return 0;
}

static int part_vec_find_indexes(const struct part_vec *vec, const struct bitset *indexes, size_t *index_out)
{
    size_t i;

    for (i = 0; i < vec->count; i++) {
        if (bitset_equals(&vec->items[i]->indexes, indexes)) {
            *index_out = i;
            return 1;
        }
    }

    return 0;
}

static uint64_t read_u64_be(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56U) |
           ((uint64_t)p[1] << 48U) |
           ((uint64_t)p[2] << 40U) |
           ((uint64_t)p[3] << 32U) |
           ((uint64_t)p[4] << 24U) |
           ((uint64_t)p[5] << 16U) |
           ((uint64_t)p[6] << 8U) |
           (uint64_t)p[7];
}

static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24U) |
           ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) |
           (uint32_t)p[3];
}

static uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32U - n));
}

static uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}

static uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t sha256_ep0(uint32_t x)
{
    return rotr32(x, 2U) ^ rotr32(x, 13U) ^ rotr32(x, 22U);
}

static uint32_t sha256_ep1(uint32_t x)
{
    return rotr32(x, 6U) ^ rotr32(x, 11U) ^ rotr32(x, 25U);
}

static uint32_t sha256_sig0(uint32_t x)
{
    return rotr32(x, 7U) ^ rotr32(x, 18U) ^ (x >> 3U);
}

static uint32_t sha256_sig1(uint32_t x)
{
    return rotr32(x, 17U) ^ rotr32(x, 19U) ^ (x >> 10U);
}

static void sha256_transform(struct sha256_ctx *ctx, const uint8_t data[64])
{
    static const uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t t1;
    uint32_t t2;
    uint32_t m[64];
    size_t i;

    for (i = 0; i < 16U; i++) {
        m[i] = read_u32_be(data + i * 4U);
    }
    for (i = 16U; i < 64U; i++) {
        m[i] = sha256_sig1(m[i - 2U]) + m[i - 7U] + sha256_sig0(m[i - 15U]) + m[i - 16U];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64U; i++) {
        t1 = h + sha256_ep1(e) + sha256_ch(e, f, g) + k[i] + m[i];
        t2 = sha256_ep0(a) + sha256_maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(struct sha256_ctx *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

static void sha256_update(struct sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64U) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512U;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(struct sha256_ctx *ctx, uint8_t out[32])
{
    uint32_t i;

    i = ctx->datalen;

    if (ctx->datalen < 56U) {
        ctx->data[i++] = 0x80U;
        while (i < 56U) {
            ctx->data[i++] = 0x00U;
        }
    } else {
        ctx->data[i++] = 0x80U;
        while (i < 64U) {
            ctx->data[i++] = 0x00U;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56U);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8U;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8U);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16U);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24U);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32U);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40U);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48U);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56U);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4U; i++) {
        out[i] = (uint8_t)((ctx->state[0] >> (24U - i * 8U)) & 0xffU);
        out[i + 4U] = (uint8_t)((ctx->state[1] >> (24U - i * 8U)) & 0xffU);
        out[i + 8U] = (uint8_t)((ctx->state[2] >> (24U - i * 8U)) & 0xffU);
        out[i + 12U] = (uint8_t)((ctx->state[3] >> (24U - i * 8U)) & 0xffU);
        out[i + 16U] = (uint8_t)((ctx->state[4] >> (24U - i * 8U)) & 0xffU);
        out[i + 20U] = (uint8_t)((ctx->state[5] >> (24U - i * 8U)) & 0xffU);
        out[i + 24U] = (uint8_t)((ctx->state[6] >> (24U - i * 8U)) & 0xffU);
        out[i + 28U] = (uint8_t)((ctx->state[7] >> (24U - i * 8U)) & 0xffU);
    }
}

static void sha256_hash(const uint8_t *data, size_t len, uint8_t out[32])
{
    struct sha256_ctx ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

static uint64_t rotl64(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro_next_u64(struct xoshiro256ss *rng)
{
    uint64_t result = rotl64(rng->s1 * 5U, 7) * 9U;
    uint64_t t = rng->s1 << 17U;

    rng->s2 ^= rng->s0;
    rng->s3 ^= rng->s1;
    rng->s1 ^= rng->s2;
    rng->s0 ^= rng->s3;
    rng->s2 ^= t;
    rng->s3 = rotl64(rng->s3, 45);

    return result;
}

static double xoshiro_next_double(struct xoshiro256ss *rng)
{
    return (double)(xoshiro_next_u64(rng) >> 11U) * 0x1.0p-53;
}

static size_t xoshiro_next_int_range(struct xoshiro256ss *rng, size_t count)
{
    size_t idx;

    if (count == 0U) {
        return 0U;
    }

    idx = (size_t)(xoshiro_next_double(rng) * (double)count);
    if (idx >= count) {
        idx = count - 1U;
    }

    return idx;
}

static void xoshiro_seed_from_bytes(struct xoshiro256ss *rng, const uint8_t *seed, size_t seed_len)
{
    uint8_t digest[32];

    sha256_hash(seed, seed_len, digest);
    rng->s0 = read_u64_be(digest + 0U);
    rng->s1 = read_u64_be(digest + 8U);
    rng->s2 = read_u64_be(digest + 16U);
    rng->s3 = read_u64_be(digest + 24U);

    if (rng->s0 == 0U && rng->s1 == 0U && rng->s2 == 0U && rng->s3 == 0U) {
        rng->s0 = 1U;
    }
}

static int choose_degree(size_t seq_len, struct xoshiro256ss *rng, size_t *degree)
{
    double *p = NULL;
    double *probs = NULL;
    size_t *aliases = NULL;
    size_t *small = NULL;
    size_t *large = NULL;
    size_t small_count = 0;
    size_t large_count = 0;
    size_t i;
    double sum = 0.0;
    double r1;
    double r2;
    size_t idx;
    size_t sample;

    *degree = 0;

    p = malloc(seq_len * sizeof(*p));
    probs = malloc(seq_len * sizeof(*probs));
    aliases = calloc(seq_len, sizeof(*aliases));
    small = malloc(seq_len * sizeof(*small));
    large = malloc(seq_len * sizeof(*large));
    if (p == NULL || probs == NULL || aliases == NULL || small == NULL || large == NULL) {
        free(p);
        free(probs);
        free(aliases);
        free(small);
        free(large);
        return 0;
    }

    for (i = 1U; i <= seq_len; i++) {
        sum += 1.0 / (double)i;
    }
    for (i = 0; i < seq_len; i++) {
        p[i] = (1.0 / (double)(i + 1U)) * (double)seq_len / sum;
    }

    for (i = seq_len; i > 0U; i--) {
        size_t idx_rev = i - 1U;
        if (p[idx_rev] < 1.0) {
            small[small_count++] = idx_rev;
        } else {
            large[large_count++] = idx_rev;
        }
    }

    while (small_count > 0U && large_count > 0U) {
        size_t a = small[--small_count];
        size_t g = large[--large_count];

        probs[a] = p[a];
        aliases[a] = g;
        p[g] = p[g] + p[a] - 1.0;

        if (p[g] < 1.0) {
            small[small_count++] = g;
        } else {
            large[large_count++] = g;
        }
    }

    while (large_count > 0U) {
        probs[large[--large_count]] = 1.0;
    }
    while (small_count > 0U) {
        probs[small[--small_count]] = 1.0;
    }

    r1 = xoshiro_next_double(rng);
    r2 = xoshiro_next_double(rng);
    idx = (size_t)((double)seq_len * r1);
    if (idx >= seq_len) {
        idx = seq_len - 1U;
    }

    sample = (r2 < probs[idx]) ? idx : aliases[idx];
    *degree = sample + 1U;

    free(p);
    free(probs);
    free(aliases);
    free(small);
    free(large);
    return 1;
}

static int choose_fragments(uint32_t seq_num, uint32_t seq_len, uint32_t checksum, struct bitset *out)
{
    struct xoshiro256ss rng;
    size_t degree;
    uint8_t seed[8];
    size_t *remaining = NULL;
    size_t remaining_len;
    size_t i;

    if (seq_len == 0U || seq_len > MAX_MULTIPART_PARTS || seq_num == 0U) {
        return 0;
    }

    if (!bitset_init(out, seq_len)) {
        return 0;
    }

    if (seq_num <= seq_len) {
        if (!bitset_set(out, (size_t)(seq_num - 1U))) {
            bitset_free(out);
            return 0;
        }
        return 1;
    }

    seed[0] = (uint8_t)((seq_num >> 24U) & 0xffU);
    seed[1] = (uint8_t)((seq_num >> 16U) & 0xffU);
    seed[2] = (uint8_t)((seq_num >> 8U) & 0xffU);
    seed[3] = (uint8_t)(seq_num & 0xffU);
    seed[4] = (uint8_t)((checksum >> 24U) & 0xffU);
    seed[5] = (uint8_t)((checksum >> 16U) & 0xffU);
    seed[6] = (uint8_t)((checksum >> 8U) & 0xffU);
    seed[7] = (uint8_t)(checksum & 0xffU);

    xoshiro_seed_from_bytes(&rng, seed, sizeof(seed));

    if (!choose_degree(seq_len, &rng, &degree)) {
        bitset_free(out);
        return 0;
    }

    remaining = malloc((size_t)seq_len * sizeof(*remaining));
    if (remaining == NULL) {
        bitset_free(out);
        return 0;
    }

    for (i = 0; i < seq_len; i++) {
        remaining[i] = i;
    }

    remaining_len = seq_len;
    for (i = 0; i < degree; i++) {
        size_t pick = xoshiro_next_int_range(&rng, remaining_len);
        size_t idx = remaining[pick];

        if (!bitset_set(out, idx)) {
            free(remaining);
            bitset_free(out);
            return 0;
        }

        remaining_len--;
        if (pick < remaining_len) {
            memmove(&remaining[pick], &remaining[pick + 1U], (remaining_len - pick) * sizeof(*remaining));
        }
    }

    free(remaining);
    return out->count > 0U;
}

static void fountain_decoder_reset(struct fountain_decoder *fd)
{
    size_t i;

    bitset_free(&fd->received_indexes);

    if (fd->simple_parts != NULL) {
        for (i = 0; i < (size_t)fd->expected_seq_len; i++) {
            fountain_part_free(fd->simple_parts[i]);
        }
        free(fd->simple_parts);
    }

    part_vec_clear(&fd->mixed_parts);
    part_vec_clear(&fd->queue);

    free(fd->result);
    memset(fd, 0, sizeof(*fd));
}

static int fountain_decoder_init(struct fountain_decoder *fd)
{
    memset(fd, 0, sizeof(*fd));
    return 1;
}

static int fountain_validate_part(struct fountain_decoder *fd, const struct ur_fountain_part *part)
{
    if (part->seq_len == 0U || part->seq_len > MAX_MULTIPART_PARTS ||
        part->message_len == 0U || part->message_len > MAX_PSBT_BYTES ||
        part->data_len == 0U || part->data_len > MAX_PSBT_BYTES ||
        part->data_len > UINT32_MAX) {
        return 0;
    }

    if (!fd->initialized) {
        fd->expected_seq_len = part->seq_len;
        fd->expected_message_len = part->message_len;
        fd->expected_checksum = part->checksum;
        fd->expected_fragment_len = (uint32_t)part->data_len;

        if (!bitset_init(&fd->received_indexes, fd->expected_seq_len)) {
            return 0;
        }

        fd->simple_parts = calloc(fd->expected_seq_len, sizeof(*fd->simple_parts));
        if (fd->simple_parts == NULL) {
            bitset_free(&fd->received_indexes);
            return 0;
        }

        fd->initialized = 1;
        return 1;
    }

    return part->seq_len == fd->expected_seq_len &&
           part->message_len == fd->expected_message_len &&
           part->checksum == fd->expected_checksum &&
           part->data_len == fd->expected_fragment_len;
}

static struct fountain_part *reduce_part_by_part(const struct fountain_part *a, const struct fountain_part *b)
{
    struct bitset new_indexes;
    uint8_t *new_data;
    size_t i;

    if (!bitset_contains_all(&a->indexes, &b->indexes)) {
        return fountain_part_clone(a);
    }

    if (!bitset_subtract(&new_indexes, &a->indexes, &b->indexes)) {
        return NULL;
    }

    new_data = malloc(a->data_len);
    if (new_data == NULL) {
        bitset_free(&new_indexes);
        return NULL;
    }

    for (i = 0; i < a->data_len; i++) {
        new_data[i] = a->data[i] ^ b->data[i];
    }

    return fountain_part_new_owned(&new_indexes, new_data, a->data_len);
}

static void fountain_reduce_mixed_by(struct fountain_decoder *fd, const struct fountain_part *by)
{
    struct part_vec new_mixed;
    size_t i;

    memset(&new_mixed, 0, sizeof(new_mixed));

    for (i = 0; i < fd->mixed_parts.count; i++) {
        struct fountain_part *reduced = reduce_part_by_part(fd->mixed_parts.items[i], by);

        fountain_part_free(fd->mixed_parts.items[i]);
        fd->mixed_parts.items[i] = NULL;

        if (reduced == NULL) {
            continue;
        }

        if (reduced->indexes.count == 0U) {
            fountain_part_free(reduced);
        } else if (reduced->indexes.count == 1U) {
            if (!part_vec_push(&fd->queue, reduced)) {
                fountain_part_free(reduced);
            }
        } else {
            size_t existing_idx;
            if (part_vec_find_indexes(&new_mixed, &reduced->indexes, &existing_idx)) {
                fountain_part_free(new_mixed.items[existing_idx]);
                new_mixed.items[existing_idx] = reduced;
            } else if (!part_vec_push(&new_mixed, reduced)) {
                fountain_part_free(reduced);
            }
        }
    }

    free(fd->mixed_parts.items);
    fd->mixed_parts = new_mixed;
}

static void fountain_complete_with_result(struct fountain_decoder *fd)
{
    uint8_t *joined = NULL;
    uint8_t *message = NULL;
    size_t joined_len;
    size_t i;
    size_t off = 0;
    uint32_t crc;

    fd->complete = 1;
    fd->success = 0;

    if (!safe_mul_size((size_t)fd->expected_seq_len, (size_t)fd->expected_fragment_len, &joined_len)) {
        return;
    }
    if (joined_len < fd->expected_message_len || fd->expected_message_len > MAX_PSBT_BYTES) {
        return;
    }

    joined = malloc(joined_len);
    if (joined == NULL) {
        return;
    }

    for (i = 0; i < (size_t)fd->expected_seq_len; i++) {
        struct fountain_part *p = fd->simple_parts[i];
        if (p == NULL || p->data_len != fd->expected_fragment_len) {
            free(joined);
            return;
        }
        memcpy(joined + off, p->data, p->data_len);
        off += p->data_len;
    }

    message = malloc(fd->expected_message_len);
    if (message == NULL) {
        free(joined);
        return;
    }
    memcpy(message, joined, fd->expected_message_len);
    free(joined);

    crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, message, fd->expected_message_len);

    if (crc == fd->expected_checksum) {
        fd->result = message;
        fd->result_len = fd->expected_message_len;
        fd->success = 1;
    } else {
        free(message);
    }
}

static void fountain_process_simple_part(struct fountain_decoder *fd, struct fountain_part *part)
{
    size_t idx;

    idx = bitset_first_index(&part->indexes);
    if (idx == SIZE_MAX || idx >= fd->expected_seq_len) {
        fountain_part_free(part);
        return;
    }

    if (bitset_test(&fd->received_indexes, idx)) {
        fountain_part_free(part);
        return;
    }

    fd->simple_parts[idx] = part;
    bitset_set(&fd->received_indexes, idx);

    if (fd->received_indexes.count == fd->expected_seq_len) {
        fountain_complete_with_result(fd);
    } else {
        fountain_reduce_mixed_by(fd, part);
    }
}

static void fountain_process_mixed_part(struct fountain_decoder *fd, struct fountain_part *part)
{
    size_t i;

    if (part_vec_contains_indexes(&fd->mixed_parts, &part->indexes)) {
        fountain_part_free(part);
        return;
    }

    for (i = 0; i < (size_t)fd->expected_seq_len; i++) {
        struct fountain_part *simple = fd->simple_parts[i];
        struct fountain_part *tmp;

        if (simple == NULL) {
            continue;
        }

        tmp = reduce_part_by_part(part, simple);
        fountain_part_free(part);
        part = tmp;
        if (part == NULL) {
            return;
        }
        if (part->indexes.count == 0U) {
            fountain_part_free(part);
            return;
        }
    }

    for (i = 0; i < fd->mixed_parts.count; i++) {
        struct fountain_part *tmp = reduce_part_by_part(part, fd->mixed_parts.items[i]);

        fountain_part_free(part);
        part = tmp;
        if (part == NULL) {
            return;
        }
        if (part->indexes.count == 0U) {
            fountain_part_free(part);
            return;
        }
    }

    if (part->indexes.count == 1U) {
        if (!part_vec_push(&fd->queue, part)) {
            fountain_part_free(part);
        }
        return;
    }

    fountain_reduce_mixed_by(fd, part);
    {
        size_t existing_idx;
        if (part_vec_find_indexes(&fd->mixed_parts, &part->indexes, &existing_idx)) {
            fountain_part_free(fd->mixed_parts.items[existing_idx]);
            fd->mixed_parts.items[existing_idx] = part;
        } else if (!part_vec_push(&fd->mixed_parts, part)) {
            fountain_part_free(part);
        }
    }
}

static int fountain_receive_part(struct fountain_decoder *fd, const struct ur_fountain_part *encoder_part)
{
    struct bitset indexes;
    uint8_t *part_data;
    struct fountain_part *part;

    if (fd->complete) {
        return fd->success;
    }

    if (!fountain_validate_part(fd, encoder_part)) {
        return 0;
    }

    memset(&indexes, 0, sizeof(indexes));
    if (!choose_fragments(encoder_part->seq_num, encoder_part->seq_len, encoder_part->checksum, &indexes)) {
        return 0;
    }

    part_data = malloc(encoder_part->data_len);
    if (part_data == NULL) {
        bitset_free(&indexes);
        return 0;
    }
    memcpy(part_data, encoder_part->data, encoder_part->data_len);

    part = fountain_part_new_owned(&indexes, part_data, encoder_part->data_len);
    if (part == NULL) {
        return 0;
    }

    if (!part_vec_push(&fd->queue, part)) {
        fountain_part_free(part);
        return 0;
    }

    while (!fd->complete && fd->queue.count > 0U) {
        struct fountain_part *queued = part_vec_pop_front(&fd->queue);
        if (queued == NULL) {
            break;
        }

        if (queued->indexes.count == 1U) {
            fountain_process_simple_part(fd, queued);
        } else {
            fountain_process_mixed_part(fd, queued);
        }
    }

    return fd->complete && fd->success;
}

static int parse_uint_dec(const char *s, size_t len, uint32_t *out)
{
    uint64_t v = 0;
    size_t i;

    if (len == 0) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        v = v * 10U + (uint64_t)(s[i] - '0');
        if (v > UINT32_MAX) {
            return 0;
        }
    }

    *out = (uint32_t)v;
    return 1;
}

static int parse_base36_2(const char *s, size_t *out)
{
    int hi;
    int lo;

    if (s[0] >= '0' && s[0] <= '9') {
        hi = s[0] - '0';
    } else if (s[0] >= 'A' && s[0] <= 'Z') {
        hi = s[0] - 'A' + 10;
    } else if (s[0] >= 'a' && s[0] <= 'z') {
        hi = s[0] - 'a' + 10;
    } else {
        return 0;
    }

    if (s[1] >= '0' && s[1] <= '9') {
        lo = s[1] - '0';
    } else if (s[1] >= 'A' && s[1] <= 'Z') {
        lo = s[1] - 'A' + 10;
    } else if (s[1] >= 'a' && s[1] <= 'z') {
        lo = s[1] - 'a' + 10;
    } else {
        return 0;
    }

    *out = (size_t)(hi * 36 + lo);
    return 1;
}

static char *dup_slice(const char *s, size_t len)
{
    char *copy = malloc(len + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

static int ur_type_supported(const char *type)
{
    return strcmp(type, "crypto-psbt") == 0 || strcmp(type, "psbt") == 0 || strcmp(type, "bytes") == 0;
}

static int ur_extract_psbt_from_cbor(const char *type, const uint8_t *cbor, size_t cbor_len, uint8_t **out, size_t *out_len)
{
    uint8_t *payload = NULL;
    size_t payload_len = 0;

    (void)type;

    *out = NULL;
    *out_len = 0;

    if (!cbor_extract_single_bstr(cbor, cbor_len, &payload, &payload_len)) {
        return 0;
    }

    if (!is_psbt_magic(payload, payload_len)) {
        free(payload);
        return 0;
    }

    *out = payload;
    *out_len = payload_len;
    return 1;
}

static void ur_state_reset(struct ur_state *state)
{
    state->active = 0;
    state->type[0] = '\0';
    state->last_update = 0;
    fountain_decoder_reset(&state->fountain);
    fountain_decoder_init(&state->fountain);
}

static int parse_ur_sequence(const char *seq, uint32_t *seq_num, uint32_t *seq_len)
{
    const char *dash = strchr(seq, '-');
    size_t left_len;
    size_t right_len;

    if (dash == NULL) {
        return 0;
    }

    left_len = (size_t)(dash - seq);
    right_len = strlen(dash + 1U);

    if (!parse_uint_dec(seq, left_len, seq_num) || !parse_uint_dec(dash + 1U, right_len, seq_len)) {
        return 0;
    }

    if (*seq_num == 0U || *seq_len == 0U) {
        return 0;
    }

    return 1;
}

static int ur_consume_text(struct ur_state *state, const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    char *lc = NULL;
    char *path;
    char *slash1;
    char *type;
    char *rest;
    char *slash2;
    char *slash3;

    *out = NULL;
    *out_len = 0;

    if (!starts_with_ci(text, text_len, "ur:")) {
        return 0;
    }

    lc = dup_slice(text, text_len);
    if (lc == NULL) {
        return 0;
    }

    {
        size_t i;
        for (i = 0; i < text_len; i++) {
            lc[i] = (char)lower_char((unsigned char)lc[i]);
        }
    }

    path = lc + 3U;
    slash1 = strchr(path, '/');
    if (slash1 == NULL) {
        free(lc);
        return 0;
    }

    *slash1 = '\0';
    type = path;
    rest = slash1 + 1U;

    if (!ur_type_supported(type)) {
        free(lc);
        return 0;
    }

    slash2 = strchr(rest, '/');
    if (slash2 == NULL) {
        uint8_t *cbor = NULL;
        size_t cbor_len = 0;
        int ok;

        if (!bytewords_minimal_decode_alloc(rest, strlen(rest), &cbor, &cbor_len)) {
            free(lc);
            return 0;
        }

        ok = ur_extract_psbt_from_cbor(type, cbor, cbor_len, out, out_len);
        free(cbor);
        free(lc);
        return ok;
    }

    *slash2 = '\0';
    slash3 = strchr(slash2 + 1U, '/');
    if (slash3 != NULL) {
        free(lc);
        return 0;
    }

    {
        uint32_t seq_num;
        uint32_t seq_len;
        uint8_t *part_cbor = NULL;
        size_t part_cbor_len = 0;
        struct ur_fountain_part part;
        int have_result = 0;

        memset(&part, 0, sizeof(part));

        if (!parse_ur_sequence(rest, &seq_num, &seq_len)) {
            free(lc);
            return 0;
        }

        if (!bytewords_minimal_decode_alloc(slash2 + 1U, strlen(slash2 + 1U), &part_cbor, &part_cbor_len)) {
            free(lc);
            return 0;
        }

        if (!cbor_parse_fountain_part(part_cbor, part_cbor_len, &part)) {
            free(part_cbor);
            free(lc);
            return 0;
        }

        free(part_cbor);

        if (part.seq_num != seq_num || part.seq_len != seq_len) {
            ur_fountain_part_free(&part);
            free(lc);
            return 0;
        }

        if (!state->active || strcmp(state->type, type) != 0) {
            ur_state_reset(state);
            state->active = 1;
            strncpy(state->type, type, sizeof(state->type) - 1U);
            state->type[sizeof(state->type) - 1U] = '\0';
        }

        state->last_update = time(NULL);
        have_result = fountain_receive_part(&state->fountain, &part);
        ur_fountain_part_free(&part);

        if (have_result && state->fountain.result != NULL && state->fountain.result_len > 0U) {
            int ok = ur_extract_psbt_from_cbor(state->type, state->fountain.result, state->fountain.result_len, out, out_len);
            if (ok) {
                ur_state_reset(state);
                free(lc);
                return 1;
            }

            ur_state_reset(state);
        }
    }

    free(lc);
    return 0;
}

static void bbqr_state_reset(struct bbqr_state *state)
{
    size_t i;

    if (state->parts != NULL) {
        for (i = 0; i < state->total_parts; i++) {
            free(state->parts[i]);
        }
    }

    free(state->parts);
    free(state->part_lens);
    memset(state, 0, sizeof(*state));
}

static int bbqr_state_start(struct bbqr_state *state, char encoding, char type, size_t total_parts)
{
    if (total_parts == 0U || total_parts > MAX_MULTIPART_PARTS) {
        return 0;
    }

    bbqr_state_reset(state);

    state->parts = calloc(total_parts, sizeof(*state->parts));
    state->part_lens = calloc(total_parts, sizeof(*state->part_lens));
    if (state->parts == NULL || state->part_lens == NULL) {
        bbqr_state_reset(state);
        return 0;
    }

    state->active = 1;
    state->encoding = encoding;
    state->type = type;
    state->total_parts = total_parts;
    state->last_update = time(NULL);
    return 1;
}

static int decode_bbqr_complete(const struct bbqr_state *state, uint8_t **out, size_t *out_len)
{
    char *joined = NULL;
    size_t joined_len = 0;
    size_t off = 0;
    size_t i;
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;

    *out = NULL;
    *out_len = 0;

    if (state->type != 'P') {
        return 0;
    }

    for (i = 0; i < state->total_parts; i++) {
        if (state->parts[i] == NULL) {
            return 0;
        }
        if (!safe_add_size(joined_len, state->part_lens[i], &joined_len) || joined_len > (MAX_PSBT_BYTES * 8U)) {
            return 0;
        }
    }

    joined = malloc(joined_len + 1U);
    if (joined == NULL) {
        return 0;
    }

    for (i = 0; i < state->total_parts; i++) {
        memcpy(joined + off, state->parts[i], state->part_lens[i]);
        off += state->part_lens[i];
    }
    joined[joined_len] = '\0';

    if (state->encoding == 'H') {
        if (!hex_decode_alloc(joined, joined_len, &decoded, &decoded_len)) {
            free(joined);
            return 0;
        }
    } else if (state->encoding == '2') {
        if (!base32_decode_alloc(joined, joined_len, &decoded, &decoded_len)) {
            free(joined);
            return 0;
        }
    } else if (state->encoding == 'Z') {
        uint8_t *compressed = NULL;
        size_t compressed_len = 0;

        if (!base32_decode_alloc(joined, joined_len, &compressed, &compressed_len)) {
            free(joined);
            return 0;
        }

        if (!inflate_raw_alloc(compressed, compressed_len, &decoded, &decoded_len)) {
            free(compressed);
            free(joined);
            return 0;
        }
        free(compressed);
    } else {
        free(joined);
        return 0;
    }

    free(joined);

    if (!is_psbt_magic(decoded, decoded_len)) {
        free(decoded);
        return 0;
    }

    *out = decoded;
    *out_len = decoded_len;
    return 1;
}

static int bbqr_consume_text(struct bbqr_state *state, const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    size_t total_parts;
    size_t part_num;
    char encoding;
    char type;
    const char *payload;
    size_t payload_len;

    *out = NULL;
    *out_len = 0;

    if (text_len < 8U) {
        return 0;
    }
    if (!starts_with_ci(text, text_len, "B$")) {
        return 0;
    }

    encoding = (char)toupper((unsigned char)text[2]);
    type = (char)toupper((unsigned char)text[3]);
    if (!parse_base36_2(text + 4U, &total_parts) || !parse_base36_2(text + 6U, &part_num)) {
        return 0;
    }

    payload = text + 8U;
    payload_len = text_len - 8U;
    if (payload_len == 0U || total_parts == 0U || part_num >= total_parts) {
        return 0;
    }

    if (!state->active || state->encoding != encoding || state->type != type || state->total_parts != total_parts) {
        if (!bbqr_state_start(state, encoding, type, total_parts)) {
            return 0;
        }
    }

    state->last_update = time(NULL);

    if (state->parts[part_num] == NULL) {
        state->parts[part_num] = dup_slice(payload, payload_len);
        if (state->parts[part_num] == NULL) {
            bbqr_state_reset(state);
            return 0;
        }
        state->part_lens[part_num] = payload_len;
        state->received_parts++;
    }

    if (state->received_parts == state->total_parts) {
        int ok = decode_bbqr_complete(state, out, out_len);
        bbqr_state_reset(state);
        return ok;
    }

    return 0;
}

static int parse_pofn_fragment(const char *text,
                               size_t text_len,
                               size_t *index_out,
                               size_t *total_out,
                               const char **payload_out,
                               size_t *payload_len_out)
{
    size_t pos = 0;
    uint32_t idx = 0;
    uint32_t total = 0;
    size_t idx_start;
    size_t total_start;

    if (text_len < 6U) {
        return 0;
    }
    if (lower_char((unsigned char)text[pos]) != 'p') {
        return 0;
    }
    pos++;

    idx_start = pos;
    while (pos < text_len && isdigit((unsigned char)text[pos])) {
        pos++;
    }
    if (pos == idx_start) {
        return 0;
    }
    if (!parse_uint_dec(text + idx_start, pos - idx_start, &idx) || idx == 0U) {
        return 0;
    }

    if (pos + 2U >= text_len) {
        return 0;
    }
    if (lower_char((unsigned char)text[pos]) != 'o' || lower_char((unsigned char)text[pos + 1U]) != 'f') {
        return 0;
    }
    pos += 2U;

    total_start = pos;
    while (pos < text_len && isdigit((unsigned char)text[pos])) {
        pos++;
    }
    if (pos == total_start) {
        return 0;
    }
    if (!parse_uint_dec(text + total_start, pos - total_start, &total) || total == 0U || idx > total) {
        return 0;
    }

    if (pos >= text_len || !isspace((unsigned char)text[pos])) {
        return 0;
    }
    while (pos < text_len && isspace((unsigned char)text[pos])) {
        pos++;
    }
    if (pos >= text_len) {
        return 0;
    }

    *index_out = (size_t)idx;
    *total_out = (size_t)total;
    *payload_out = text + pos;
    *payload_len_out = text_len - pos;
    return 1;
}

static void pofn_state_reset(struct pofn_state *state)
{
    size_t i;

    if (state->parts != NULL) {
        for (i = 0; i < state->total_parts; i++) {
            free(state->parts[i]);
        }
    }
    free(state->parts);
    free(state->part_lens);
    memset(state, 0, sizeof(*state));
}

static int pofn_state_start(struct pofn_state *state, size_t total_parts)
{
    if (total_parts == 0U || total_parts > MAX_MULTIPART_PARTS) {
        return 0;
    }

    pofn_state_reset(state);

    state->parts = calloc(total_parts, sizeof(*state->parts));
    state->part_lens = calloc(total_parts, sizeof(*state->part_lens));
    if (state->parts == NULL || state->part_lens == NULL) {
        pofn_state_reset(state);
        return 0;
    }

    state->active = 1;
    state->total_parts = total_parts;
    state->last_update = time(NULL);
    return 1;
}

static int decode_direct_text_psbt(const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;

    *out = NULL;
    *out_len = 0;

    if (text_len == 0U) {
        return 0;
    }

    if (starts_with_ci(text, text_len, "cHNidP") && base64_decode_alloc(text, text_len, &decoded, &decoded_len) && is_psbt_magic(decoded, decoded_len)) {
        *out = decoded;
        *out_len = decoded_len;
        return 1;
    }
    free(decoded);
    decoded = NULL;

    if (looks_like_hex(text, text_len) && hex_decode_alloc(text, text_len, &decoded, &decoded_len) && is_psbt_magic(decoded, decoded_len)) {
        *out = decoded;
        *out_len = decoded_len;
        return 1;
    }
    free(decoded);
    decoded = NULL;

    if (looks_like_base64(text, text_len) && base64_decode_alloc(text, text_len, &decoded, &decoded_len) && is_psbt_magic(decoded, decoded_len)) {
        *out = decoded;
        *out_len = decoded_len;
        return 1;
    }
    free(decoded);
    decoded = NULL;

    if (looks_like_base43(text, text_len) && base43_decode_alloc(text, text_len, &decoded, &decoded_len) && is_psbt_magic(decoded, decoded_len)) {
        *out = decoded;
        *out_len = decoded_len;
        return 1;
    }

    free(decoded);
    return 0;
}

static int pofn_consume_text(struct pofn_state *state, const char *text, size_t text_len, uint8_t **out, size_t *out_len)
{
    size_t idx;
    size_t total;
    const char *payload;
    size_t payload_len;

    *out = NULL;
    *out_len = 0;

    if (!parse_pofn_fragment(text, text_len, &idx, &total, &payload, &payload_len)) {
        return 0;
    }

    if (!state->active || state->total_parts != total) {
        if (!pofn_state_start(state, total)) {
            return 0;
        }
    }

    state->last_update = time(NULL);

    if (state->parts[idx - 1U] == NULL) {
        state->parts[idx - 1U] = dup_slice(payload, payload_len);
        if (state->parts[idx - 1U] == NULL) {
            pofn_state_reset(state);
            return 0;
        }
        state->part_lens[idx - 1U] = payload_len;
        state->received_parts++;
    }

    if (state->received_parts == state->total_parts) {
        char *joined = NULL;
        size_t joined_len = 0;
        size_t off = 0;
        size_t i;
        int ok;

        for (i = 0; i < state->total_parts; i++) {
            if (state->parts[i] == NULL ||
                !safe_add_size(joined_len, state->part_lens[i], &joined_len) ||
                joined_len > (MAX_PSBT_BYTES * 4U)) {
                pofn_state_reset(state);
                return 0;
            }
        }

        joined = malloc(joined_len + 1U);
        if (joined == NULL) {
            pofn_state_reset(state);
            return 0;
        }

        for (i = 0; i < state->total_parts; i++) {
            memcpy(joined + off, state->parts[i], state->part_lens[i]);
            off += state->part_lens[i];
        }
        joined[joined_len] = '\0';

        ok = decode_direct_text_psbt(joined, joined_len, out, out_len);
        free(joined);
        pofn_state_reset(state);
        return ok;
    }

    return 0;
}

static void expire_stale_multipart(struct psbt_payload_decoder *decoder)
{
    time_t now = time(NULL);

    if (decoder->ur.active && now - decoder->ur.last_update > MULTIPART_STALE_SECONDS) {
        ur_state_reset(&decoder->ur);
    }
    if (decoder->bbqr.active && now - decoder->bbqr.last_update > MULTIPART_STALE_SECONDS) {
        bbqr_state_reset(&decoder->bbqr);
    }
    if (decoder->pofn.active && now - decoder->pofn.last_update > MULTIPART_STALE_SECONDS) {
        pofn_state_reset(&decoder->pofn);
    }
}

static int normalize_psbt_to_out(const uint8_t *psbt, size_t psbt_len, char *out, size_t out_len)
{
    if (!is_psbt_magic(psbt, psbt_len)) {
        return 0;
    }
    return base64_encode(psbt, psbt_len, out, out_len);
}

struct psbt_payload_decoder *psbt_payload_decoder_create(void)
{
    struct psbt_payload_decoder *decoder;

    decoder = calloc(1, sizeof(*decoder));
    if (decoder == NULL) {
        return NULL;
    }

    fountain_decoder_init(&decoder->ur.fountain);
    return decoder;
}

void psbt_payload_decoder_destroy(struct psbt_payload_decoder *decoder)
{
    if (decoder == NULL) {
        return;
    }

    ur_state_reset(&decoder->ur);
    bbqr_state_reset(&decoder->bbqr);
    pofn_state_reset(&decoder->pofn);
    free(decoder);
}

int psbt_payload_decoder_consume(struct psbt_payload_decoder *decoder,
                                const uint8_t *payload,
                                size_t payload_len,
                                char *out,
                                size_t out_len)
{
    char *text = NULL;
    const char *trimmed;
    size_t trimmed_len;
    uint8_t *psbt = NULL;
    size_t psbt_len = 0;
    int ok;

    if (decoder == NULL || payload == NULL || payload_len == 0U || out == NULL || out_len == 0U) {
        return 0;
    }

    expire_stale_multipart(decoder);

    if (is_psbt_magic(payload, payload_len)) {
        return normalize_psbt_to_out(payload, payload_len, out, out_len);
    }

    if (payload_len > MAX_TEXT_FRAGMENT || !payload_is_text(payload, payload_len)) {
        return 0;
    }

    text = malloc(payload_len + 1U);
    if (text == NULL) {
        return 0;
    }

    memcpy(text, payload, payload_len);
    text[payload_len] = '\0';

    trim_ascii_ws(text, payload_len, &trimmed, &trimmed_len);
    if (trimmed_len == 0U) {
        free(text);
        return 0;
    }

    ok = ur_consume_text(&decoder->ur, trimmed, trimmed_len, &psbt, &psbt_len);
    if (ok) {
        int emitted = normalize_psbt_to_out(psbt, psbt_len, out, out_len);
        free(psbt);
        free(text);
        return emitted;
    }

    ok = bbqr_consume_text(&decoder->bbqr, trimmed, trimmed_len, &psbt, &psbt_len);
    if (ok) {
        int emitted = normalize_psbt_to_out(psbt, psbt_len, out, out_len);
        free(psbt);
        free(text);
        return emitted;
    }

    ok = pofn_consume_text(&decoder->pofn, trimmed, trimmed_len, &psbt, &psbt_len);
    if (ok) {
        int emitted = normalize_psbt_to_out(psbt, psbt_len, out, out_len);
        free(psbt);
        free(text);
        return emitted;
    }

    ok = decode_direct_text_psbt(trimmed, trimmed_len, &psbt, &psbt_len);
    if (ok) {
        int emitted = normalize_psbt_to_out(psbt, psbt_len, out, out_len);
        free(psbt);
        free(text);
        return emitted;
    }

    free(text);
    return 0;
}
