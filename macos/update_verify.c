/* update_verify.c — minisign + SHA-256 checks for the in-app updater. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "update_verify.h"
#include "third_party/monocypher/monocypher.h"
#include "third_party/monocypher/monocypher-ed25519.h"

/* ---- base64 ---- */
static int b64val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decodes exactly one base64 token (stops at whitespace/NUL); returns bytes or -1. */
static int b64decode(const char *in, unsigned char *out, int cap)
{
    int n = 0, bits = 0, acc = 0, pad = 0;
    for (; *in && *in != '\n' && *in != '\r' && *in != ' '; in++) {
        if (*in == '=') { pad++; continue; }
        if (pad) return -1;
        int v = b64val((unsigned char)*in);
        if (v < 0) return -1;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= cap) return -1;
            out[n++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    return n;
}

/* Copy line idx (0-based) of text into out without the newline. */
static int get_line(const char *text, int idx, char *out, size_t cap)
{
    const char *p = text;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, '\n');
        if (!p) return -1;
        p++;
    }
    size_t n = strcspn(p, "\r\n");
    if (n >= cap) return -1;
    memcpy(out, p, n);
    out[n] = 0;
    return 0;
}

int minisign_verify(const char *pub_b64, const unsigned char *msg, size_t msg_len,
                    const char *sig_text)
{
    unsigned char pk[64], sig[96], global[80];
    char line[1024];
    if (b64decode(pub_b64, pk, sizeof(pk)) != 42 || pk[0] != 'E' || pk[1] != 'd') return -1;
    if (get_line(sig_text, 0, line, sizeof(line)) || strncmp(line, "untrusted comment:", 18)) return -1;
    if (get_line(sig_text, 1, line, sizeof(line)) || b64decode(line, sig, sizeof(sig)) != 74) return -1;
    if (memcmp(sig + 2, pk + 2, 8) != 0) return -1; /* different key id */

    int ok;
    if (sig[0] == 'E' && sig[1] == 'D') {
        unsigned char h[64];
        crypto_blake2b(h, 64, msg, msg_len);
        ok = crypto_ed25519_check(sig + 10, pk + 10, h, 64) == 0;
    } else if (sig[0] == 'E' && sig[1] == 'd') {
        ok = crypto_ed25519_check(sig + 10, pk + 10, msg, msg_len) == 0;
    } else {
        return -1;
    }
    if (!ok) return -1;

    /* trusted comment: signature || comment, signed with the same key */
    if (get_line(sig_text, 2, line, sizeof(line)) || strncmp(line, "trusted comment: ", 17)) return -1;
    size_t tlen = strlen(line + 17);
    unsigned char buf[64 + 1024];
    memcpy(buf, sig + 10, 64);
    memcpy(buf + 64, line + 17, tlen);
    if (get_line(sig_text, 3, line, sizeof(line)) || b64decode(line, global, sizeof(global)) != 64) return -1;
    return crypto_ed25519_check(global, pk + 10, buf, 64 + tlen) == 0 ? 0 : -1;
}

/* ---- SHA-256 (FIPS 180-4) ---- */
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

typedef struct { uint32_t h[8]; uint64_t len; unsigned char buf[64]; size_t n; } sha256_ctx;
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
    uint32_t w[64], a, b, d, e, f, g, h, cc, t1, t2;
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 64; i++) {
        t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha256_init(sha256_ctx *c)
{
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->h, iv, sizeof(iv));
    c->len = 0; c->n = 0;
}

static void sha256_update(sha256_ctx *c, const unsigned char *p, size_t len)
{
    c->len += len;
    while (len) {
        size_t k = 64 - c->n < len ? 64 - c->n : len;
        memcpy(c->buf + c->n, p, k);
        c->n += k; p += k; len -= k;
        if (c->n == 64) { sha256_block(c, c->buf); c->n = 0; }
    }
}

static void sha256_final(sha256_ctx *c, char out[65])
{
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80, z = 0, l[8];
    sha256_update(c, &pad, 1);
    while (c->n != 56) sha256_update(c, &z, 1);
    for (int i = 0; i < 8; i++) l[i] = (unsigned char)(bits >> (56 - 8*i));
    sha256_update(c, l, 8);
    for (int i = 0; i < 8; i++) snprintf(out + 8*i, 9, "%08x", c->h[i]);
}

void sha256_hex(const unsigned char *data, size_t len, char out[65])
{
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_final(&c, out);
}

int sha256_file_hex(const char *path, char out[65])
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    sha256_ctx c;
    unsigned char buf[65536];
    size_t n;
    sha256_init(&c);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) sha256_update(&c, buf, n);
    int err = ferror(f);
    fclose(f);
    if (err) return -1;
    sha256_final(&c, out);
    return 0;
}

int checksum_for(const char *sums, const char *name, char out[65])
{
    const char *p = sums;
    size_t nl = strlen(name);
    while (p && *p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len > 66) {
            const char *fn = p + 64;
            int hex = 1;
            for (int i = 0; i < 64; i++) {
                char ch = p[i];
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) hex = 0;
            }
            if (hex && *fn == ' ') {
                fn++;
                if (*fn == ' ' || *fn == '*') fn++;
                size_t fl = len - (size_t)(fn - p);
                if (fl && fn[fl-1] == '\r') fl--;
                if (fl == nl && memcmp(fn, name, nl) == 0) {
                    for (int i = 0; i < 64; i++) out[i] = (char)(p[i] | 0x20);
                    out[64] = 0;
                    return 0;
                }
            }
        }
        p = e ? e + 1 : NULL;
    }
    return -1;
}

int update_url_trusted(const char *url)
{
    static const char prefix[] = "https://github.com/sbj-ee/MAC48GX/releases/download/";
    if (strncmp(url, prefix, sizeof(prefix) - 1) != 0) return 0;
    for (const char *p = url; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              strchr("-._~/:%+", c)))
            return 0;
    }
    return strstr(url, "..") == NULL;
}

int release_asset_url(const char *json, const char *name, char *url, size_t url_len)
{
    char pat[256];
    snprintf(pat, sizeof(pat), "\"name\":\"%s\"", name);
    char spaced[256];
    snprintf(spaced, sizeof(spaced), "\"name\": \"%s\"", name);
    const char *p = strstr(json, pat);
    if (!p) p = strstr(json, spaced);
    if (!p) return -1;
    const char *u = strstr(p, "\"browser_download_url\"");
    if (!u) return -1;
    /* must belong to the same asset object */
    const char *close = strchr(p, '}');
    if (close && close < u) return -1;
    u = strchr(u + 22, '"');
    if (!u) return -1;
    u++;
    size_t n = strcspn(u, "\"");
    if (n == 0 || n >= url_len) return -1;
    memcpy(url, u, n);
    url[n] = 0;
    return update_url_trusted(url) ? 0 : -1;
}

void release_dmg_name(const char *tag, char *out, size_t out_len)
{
    snprintf(out, out_len, "MAC48GX-%s.dmg", tag);
}
