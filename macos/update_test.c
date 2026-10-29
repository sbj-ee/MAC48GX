/* update_test.c — headless tests for the signed updater (no SDL, no network).
 * Run from macos/: make test-update */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "update_check.h"
#include "update_verify.h"
#include "third_party/monocypher/monocypher.h"
#include "third_party/monocypher/monocypher-ed25519.h"

static int fails = 0, passes = 0;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *TEST_PUB = "RWSLgkbIFSEuRS5xAEv9bP6Ma6NWSf7wuuDwLKeT++2GUiloG7OAZjVS";

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing fixture %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(2);
    b[n] = 0;
    fclose(f);
    if (len) *len = (size_t)n;
    return b;
}

static void b64(const unsigned char *in, int n, char *out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        int v = in[i] << 16 | (i + 1 < n ? in[i+1] << 8 : 0) | (i + 2 < n ? in[i+2] : 0);
        out[o++] = t[v >> 18 & 63]; out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}

/* ---- a throwaway key and a mock "GitHub" made of local files ---- */
static unsigned char sk[64], pk[32];
static const unsigned char keyid[8] = {1,2,3,4,5,6,7,8};
static char pub_b64[64];
static char srcdir[256];

static void make_minisig(const unsigned char *msg, size_t len, const char *trusted, char *out, size_t cap)
{
    unsigned char h[64], sig[74], s2[64], buf[64 + 256];
    char a[128], b[128];
    crypto_blake2b(h, 64, msg, len);
    memcpy(sig, "ED", 2); memcpy(sig + 2, keyid, 8);
    crypto_ed25519_sign(sig + 10, sk, h, 64);
    memcpy(buf, sig + 10, 64); memcpy(buf + 64, trusted, strlen(trusted));
    crypto_ed25519_sign(s2, sk, buf, 64 + strlen(trusted));
    b64(sig, 74, a); b64(s2, 64, b);
    snprintf(out, cap, "untrusted comment: test\n%s\ntrusted comment: %s\n%s\n", a, trusted, b);
}

/* fetch() for tests: URL https://github.com/sbj-ee/MAC48GX/releases/download/v9.0.0/<name>
 * copies srcdir/<name>. */
static int fake_fetch(const char *url, const char *dest)
{
    if (!update_url_trusted(url)) return -1;
    const char *name = strrchr(url, '/') + 1;
    char src[512];
    snprintf(src, sizeof(src), "%s/%s", srcdir, name);
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dest, "wb");
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    return 0;
}

static void write_file(const char *dir, const char *name, const void *data, size_t len)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "wb"); fwrite(data, 1, len, f); fclose(f);
}

static const char *JSON =
    "{\"tag_name\":\"v9.0.0\",\"html_url\":\"https://github.com/sbj-ee/MAC48GX/releases/tag/v9.0.0\",\"assets\":["
    "{\"name\":\"MAC48GX-v9.0.0.dmg\",\"browser_download_url\":\"https://github.com/sbj-ee/MAC48GX/releases/download/v9.0.0/MAC48GX-v9.0.0.dmg\"},"
    "{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"https://github.com/sbj-ee/MAC48GX/releases/download/v9.0.0/SHA256SUMS\"},"
    "{\"name\":\"SHA256SUMS.minisig\",\"browser_download_url\":\"https://github.com/sbj-ee/MAC48GX/releases/download/v9.0.0/SHA256SUMS.minisig\"}]}";

static void setup_release(const char *dmg_body, const char *listed_body, int sign_ok)
{
    char h[65], sums[256], sig[1024];
    sha256_hex((const unsigned char *)listed_body, strlen(listed_body), h);
    snprintf(sums, sizeof(sums), "%s  MAC48GX-v9.0.0.dmg\n", h);
    write_file(srcdir, "MAC48GX-v9.0.0.dmg", dmg_body, strlen(dmg_body));
    write_file(srcdir, "SHA256SUMS", sums, strlen(sums));
    make_minisig((const unsigned char *)sums, strlen(sums), "timestamp:0 file:SHA256SUMS hashed", sig, sizeof(sig));
    if (!sign_ok) sig[30] = sig[30] == 'A' ? 'B' : 'A';
    write_file(srcdir, "SHA256SUMS.minisig", sig, strlen(sig));
}

int main(void)
{
    size_t n;
    char out[65];

    /* SHA-256 known answers (FIPS 180-4) */
    sha256_hex((const unsigned char *)"abc", 3, out);
    CHECK(strcmp(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    sha256_hex((const unsigned char *)"", 0, out);
    CHECK(strcmp(out, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256_hex((const unsigned char *)two, strlen(two), out);
    CHECK(strcmp(out, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);

    /* Real minisign output (fixtures from zwriter, made with minisign 0.12) */
    char *sums = slurp("tests/update/SHA256SUMS", &n);
    char *sig = slurp("tests/update/SHA256SUMS.minisig", NULL);
    CHECK(minisign_verify(TEST_PUB, (unsigned char *)sums, n, sig) == 0);
    sums[0] ^= 1;
    CHECK(minisign_verify(TEST_PUB, (unsigned char *)sums, n, sig) != 0);
    sums[0] ^= 1;
    CHECK(minisign_verify(UPDATE_SIGNING_PUBLIC_KEY, (unsigned char *)sums, n, sig) != 0);
    char *tc = strstr(sig, "timestamp:0");
    tc[10] = '1';
    CHECK(minisign_verify(TEST_PUB, (unsigned char *)sums, n, sig) != 0);
    CHECK(minisign_verify(TEST_PUB, (unsigned char *)sums, n, "garbage") != 0);
    free(sums); free(sig);
    sums = slurp("tests/update/SHA256SUMS.legacy", &n);
    sig = slurp("tests/update/SHA256SUMS.legacy.minisig", NULL);
    CHECK(minisign_verify(TEST_PUB, (unsigned char *)sums, n, sig) == 0);
    free(sums); free(sig);

    /* Embedded key is the shared release key DBC46B23C3AF0EF3 */
    CHECK(strcmp(UPDATE_SIGNING_PUBLIC_KEY, "RWTzDq/DI2vE2xMeF01QWTxP2lwwlxGL9M1UxQE4EFV5kVmTF84wwyi2") == 0);

    /* SHA256SUMS parsing */
    const char *list = "0000000000000000000000000000000000000000000000000000000000000000  a.dmg\n"
                       "ABCDEF0000000000000000000000000000000000000000000000000000000001 *MAC48GX-v1.1.0.dmg\r\n";
    CHECK(checksum_for(list, "MAC48GX-v1.1.0.dmg", out) == 0 && strncmp(out, "abcdef", 6) == 0);
    CHECK(checksum_for(list, "a.dm", out) != 0);

    /* URL policy */
    CHECK(update_url_trusted("https://github.com/sbj-ee/MAC48GX/releases/download/v1.1.0/SHA256SUMS"));
    CHECK(!update_url_trusted("http://github.com/sbj-ee/MAC48GX/releases/download/v1/x"));
    CHECK(!update_url_trusted("https://github.com/sbj-ee/MAC48GX/releases/download/v1/x;rm -rf ~"));
    CHECK(!update_url_trusted("https://github.com/sbj-ee/MAC48GX/releases/download/../../x"));
    CHECK(!update_url_trusted("https://evil.example/sbj-ee/MAC48GX/releases/download/v1/x"));

    char url[512];
    CHECK(release_asset_url(JSON, "SHA256SUMS", url, sizeof(url)) == 0 && strstr(url, "/SHA256SUMS") != NULL);
    CHECK(release_asset_url(JSON, "nothing.dmg", url, sizeof(url)) != 0);
    release_dmg_name("v1.1.0", url, sizeof(url));
    CHECK(strcmp(url, "MAC48GX-v1.1.0.dmg") == 0);
    CHECK(version_compare("v1.1.0", "v1.0.3") > 0);

    /* Whole download + verify flow against local files */
    crypto_ed25519_key_pair(sk, pk, (uint8_t[32]){42});
    unsigned char pkfile[42] = {'E', 'd'};
    memcpy(pkfile + 2, keyid, 8); memcpy(pkfile + 10, pk, 32);
    b64(pkfile, 42, pub_b64);
    char tmpl[] = "/tmp/mac48gx-update-test-XXXXXX";
    snprintf(srcdir, sizeof(srcdir), "%s", mkdtemp(tmpl));
    char dl[] = "/tmp/mac48gx-update-dl-XXXXXX";
    mkdtemp(dl);
    char path[1024], err[512];

    setup_release("fake dmg bytes", "fake dmg bytes", 1);
    CHECK(update_download_verified(JSON, "v9.0.0", dl, pub_b64, fake_fetch, path, sizeof(path), err, sizeof(err)) == 0);
    CHECK(access(path, F_OK) == 0);
    unlink(path);

    setup_release("tampered dmg", "fake dmg bytes", 1);
    CHECK(update_download_verified(JSON, "v9.0.0", dl, pub_b64, fake_fetch, path, sizeof(path), err, sizeof(err)) != 0);
    CHECK(strstr(err, "does not match") != NULL);
    char left[1024];
    snprintf(left, sizeof(left), "%s/MAC48GX-v9.0.0.dmg", dl);
    CHECK(access(left, F_OK) != 0);   /* nothing left to open */

    setup_release("fake dmg bytes", "fake dmg bytes", 0);
    CHECK(update_download_verified(JSON, "v9.0.0", dl, pub_b64, fake_fetch, path, sizeof(path), err, sizeof(err)) != 0);
    CHECK(strstr(err, "signature") != NULL);

    setup_release("fake dmg bytes", "fake dmg bytes", 1);
    CHECK(update_download_verified(JSON, "v9.0.0", dl, UPDATE_SIGNING_PUBLIC_KEY, fake_fetch, path, sizeof(path), err, sizeof(err)) != 0);

    const char *unsigned_json = "{\"tag_name\":\"v9.0.0\",\"assets\":[{\"name\":\"MAC48GX-v9.0.0.dmg\",\"browser_download_url\":\"https://github.com/sbj-ee/MAC48GX/releases/download/v9.0.0/MAC48GX-v9.0.0.dmg\"}]}";
    CHECK(update_download_verified(unsigned_json, "v9.0.0", dl, pub_b64, fake_fetch, path, sizeof(path), err, sizeof(err)) != 0);

    /* Check-at-startup preference */
    char home[] = "/tmp/mac48gx-home-XXXXXX";
    mkdtemp(home);
    setenv("HOME", home, 1);
    char d48[600];
    snprintf(d48, sizeof(d48), "%s/.droid48", home);
    mkdir(d48, 0700);
    CHECK(update_check_at_startup_enabled() == 1);
    update_set_check_at_startup(0);
    CHECK(update_check_at_startup_enabled() == 0);
    update_set_check_at_startup(1);
    CHECK(update_check_at_startup_enabled() == 1);

    printf("update tests: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
