/* update_verify.h — Verify a MAC48GX release before it is opened.
 *
 * Same flow as zwriter's updater: the release's SHA256SUMS must carry a
 * valid minisign signature (SHA256SUMS.minisig) by the release key compiled
 * in below, and the downloaded .dmg must match its line in SHA256SUMS.
 * Pure C, no SDL, so the headless tests cover it on Linux and macOS.
 */
#ifndef UPDATE_VERIFY_H
#define UPDATE_VERIFY_H

#include <stddef.h>

/* Shared release key DBC46B23C3AF0EF3 (the same key as zwriter and zedit).
 * Not secret: the secret half lives only on the maintainer's machine and in
 * this repository's MINISIGN_SECRET_KEY / MINISIGN_PASSWORD Actions secrets. */
#define UPDATE_SIGNING_PUBLIC_KEY "RWTzDq/DI2vE2xMeF01QWTxP2lwwlxGL9M1UxQE4EFV5kVmTF84wwyi2"

/* 0 when sig_text (a .minisig file) is a valid signature of msg by the
 * base64 minisign public key pub_b64, prehashed ("ED") or legacy ("Ed"),
 * including the trusted-comment signature. -1 otherwise. */
int minisign_verify(const char *pub_b64, const unsigned char *msg, size_t msg_len,
                    const char *sig_text);

/* SHA-256 of a buffer / a file as 64 lower-case hex chars (+NUL). */
void sha256_hex(const unsigned char *data, size_t len, char out[65]);
int sha256_file_hex(const char *path, char out[65]);

/* Copy the hex hash listed for name in a sha256sum file to out; 0 if found. */
int checksum_for(const char *sums, const char *name, char out[65]);

/* 1 for https://github.com/sbj-ee/MAC48GX/releases/download/... with only
 * URL-safe characters, else 0. Nothing else is ever downloaded. */
int update_url_trusted(const char *url);

/* Find the browser_download_url of the asset called name in GitHub's
 * release JSON; 0 if found and trusted. */
int release_asset_url(const char *json, const char *name, char *url, size_t url_len);

/* The .dmg a release ships: MAC48GX-v<ver>.dmg (from git describe). */
void release_dmg_name(const char *tag, char *out, size_t out_len);

#endif
