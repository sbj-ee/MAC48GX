/* update_check.c — Check GitHub for a newer MAC48GX release.
 *
 * Uses the system curl (always present on macOS) via popen, so no new
 * library dependencies. Runs on a detached thread so the UI never blocks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "update_check.h"
#include "update_verify.h"

extern char **environ;

#define RELEASES_API "https://api.github.com/repos/sbj-ee/MAC48GX/releases/latest"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

static pthread_mutex_t upd_lock = PTHREAD_MUTEX_INITIALIZER;
static int  upd_state  = UPD_IDLE;
static int  upd_silent = 0;
static char upd_tag[64];
static char upd_url[512];
static char *upd_json = NULL;     /* release JSON of the last UPD_NEWER result */
static char  upd_json_tag[64];

static int  inst_state = INST_IDLE;
static char inst_msg[1024];

int version_compare(const char *a, const char *b)
{
    while (*a && !isdigit((unsigned char)*a)) a++;
    while (*b && !isdigit((unsigned char)*b)) b++;
    for (int i = 0; i < 4; i++) {
        long x = strtol(a, (char **)&a, 10);
        long y = strtol(b, (char **)&b, 10);
        if (x != y) return x > y ? 1 : -1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!isdigit((unsigned char)*a) && !isdigit((unsigned char)*b)) break;
    }
    return 0;
}

/* Extract the string value of "key": "..." from a JSON blob (flat search). */
static int json_string(const char *json, const char *key, char *out, int out_len)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return -1;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < out_len - 1) out[n++] = *p++;
    out[n] = 0;
    return n > 0 ? 0 : -1;
}

static void *upd_thread(void *arg)
{
    (void)arg;
    int result = UPD_FAILED;
    char tag[64] = "", url[512] = "";
    FILE *f = popen("curl -fsSL --max-time 10 -H 'Accept: application/vnd.github+json' "
                    "-A MAC48GX/" APP_VERSION " " RELEASES_API " 2>/dev/null", "r");
    if (f) {
        size_t cap = 1 << 16, len = 0, n;
        char *buf = malloc(cap + 1);
        if (buf) {
            while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
                len += n;
                if (len == cap) break;
            }
            buf[len] = 0;
            if (json_string(buf, "tag_name", tag, sizeof(tag)) == 0) {
                if (json_string(buf, "html_url", url, sizeof(url)) != 0)
                    snprintf(url, sizeof(url), "https://github.com/sbj-ee/MAC48GX/releases/latest");
                result = version_compare(tag, APP_VERSION) > 0 ? UPD_NEWER : UPD_CURRENT;
            }
            if (result == UPD_NEWER) {
                pthread_mutex_lock(&upd_lock);
                free(upd_json);
                upd_json = buf;   /* kept for update_install_start() */
                snprintf(upd_json_tag, sizeof(upd_json_tag), "%s", tag);
                pthread_mutex_unlock(&upd_lock);
                buf = NULL;
            }
            free(buf);
        }
        pclose(f);
    }
    pthread_mutex_lock(&upd_lock);
    if (upd_silent && result != UPD_NEWER) result = UPD_IDLE;
    snprintf(upd_tag, sizeof(upd_tag), "%s", tag);
    snprintf(upd_url, sizeof(upd_url), "%s", url);
    upd_state = result;
    pthread_mutex_unlock(&upd_lock);
    return NULL;
}

void update_check_start(int silent)
{
    pthread_mutex_lock(&upd_lock);
    if (upd_state == UPD_RUNNING) {
        /* A manual check while a silent one runs should still report. */
        if (!silent) upd_silent = 0;
        pthread_mutex_unlock(&upd_lock);
        return;
    }
    upd_state = UPD_RUNNING;
    upd_silent = silent;
    pthread_mutex_unlock(&upd_lock);

    pthread_t t;
    if (pthread_create(&t, NULL, upd_thread, NULL) == 0) {
        pthread_detach(t);
    } else {
        pthread_mutex_lock(&upd_lock);
        upd_state = silent ? UPD_IDLE : UPD_FAILED;
        pthread_mutex_unlock(&upd_lock);
    }
}

int update_check_poll(char *tag, int tag_len, char *url, int url_len)
{
    int s;
    pthread_mutex_lock(&upd_lock);
    s = upd_state;
    if (s == UPD_NEWER || s == UPD_CURRENT || s == UPD_FAILED) {
        snprintf(tag, tag_len, "%s", upd_tag);
        snprintf(url, url_len, "%s", upd_url);
        upd_state = UPD_IDLE;
    }
    pthread_mutex_unlock(&upd_lock);
    return s;
}

/* ---------------- install ---------------- */

/* Download url to dest with /usr/bin/curl, no shell involved. */
static int curl_fetch(const char *url, const char *dest)
{
    if (!update_url_trusted(url)) return -1;
    char *argv[] = { "/usr/bin/curl", "-fsSL", "--proto", "=https", "--max-time", "600",
                     "-A", "MAC48GX/" APP_VERSION, "-o", (char *)dest, (char *)url, NULL };
    pid_t pid;
    if (posix_spawn(&pid, argv[0], NULL, NULL, argv, environ) != 0) return -1;
    int st;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

static char *read_file(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(max + 1);
    size_t n = b ? fread(b, 1, max, f) : 0;
    fclose(f);
    if (!b) return NULL;
    b[n] = 0;
    if (len) *len = n;
    return b;
}

int update_download_verified(const char *json, const char *tag, const char *dir,
                             const char *pub, update_fetch_fn fetch,
                             char *out_path, int out_len, char *err, int err_len)
{
    char dmg[128], url[512], sums_p[1024], sig_p[1024], dmg_p[1024], want[65], have[65];
    size_t sums_len = 0;
    release_dmg_name(tag, dmg, sizeof(dmg));
    snprintf(sums_p, sizeof(sums_p), "%s/SHA256SUMS", dir);
    snprintf(sig_p, sizeof(sig_p), "%s/SHA256SUMS.minisig", dir);
    snprintf(dmg_p, sizeof(dmg_p), "%s/%s", dir, dmg);

    if (release_asset_url(json, "SHA256SUMS", url, sizeof(url)) || fetch(url, sums_p)) {
        snprintf(err, err_len, "This release has no SHA256SUMS (it isn't signed), so it can't be installed from here.");
        return -1;
    }
    if (release_asset_url(json, "SHA256SUMS.minisig", url, sizeof(url)) || fetch(url, sig_p)) {
        snprintf(err, err_len, "This release has no SHA256SUMS.minisig signature, so it can't be installed from here.");
        return -1;
    }
    char *sums = read_file(sums_p, 1 << 20, &sums_len);
    char *sig = read_file(sig_p, 1 << 16, NULL);
    int ok = sums && sig && minisign_verify(pub, (unsigned char *)sums, sums_len, sig) == 0;
    free(sig);
    if (!ok) {
        free(sums);
        snprintf(err, err_len, "The release's SHA256SUMS signature is not valid. Nothing was installed.");
        return -1;
    }
    int listed = checksum_for(sums, dmg, want) == 0;
    free(sums);
    if (!listed) {
        snprintf(err, err_len, "%s is not listed in the signed SHA256SUMS.", dmg);
        return -1;
    }
    if (release_asset_url(json, dmg, url, sizeof(url)) || fetch(url, dmg_p)) {
        unlink(dmg_p);
        snprintf(err, err_len, "Couldn't download %s.", dmg);
        return -1;
    }
    if (sha256_file_hex(dmg_p, have) || strcmp(have, want) != 0) {
        unlink(dmg_p);
        snprintf(err, err_len, "%s does not match the signed SHA256SUMS. Nothing was installed.", dmg);
        return -1;
    }
    snprintf(out_path, out_len, "%s", dmg_p);
    return 0;
}

static void *inst_thread(void *arg)
{
    (void)arg;
    char *json, tag[64], dir[512], path[1024], err[512];
    pthread_mutex_lock(&upd_lock);
    json = upd_json ? strdup(upd_json) : NULL;
    snprintf(tag, sizeof(tag), "%s", upd_json_tag);
    pthread_mutex_unlock(&upd_lock);

    int state = INST_FAILED;
    char msg[1024] = "No release to install. Check for updates first.";
    const char *tmp = getenv("TMPDIR");
    snprintf(dir, sizeof(dir), "%s/MAC48GX-update-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (json && mkdtemp(dir)) {
        if (update_download_verified(json, tag, dir, UPDATE_SIGNING_PUBLIC_KEY, curl_fetch,
                                     path, sizeof(path), err, sizeof(err)) == 0) {
            char *argv[] = { "/usr/bin/open", path, NULL };
            pid_t pid;
            int st = 0;
            if (posix_spawn(&pid, argv[0], NULL, NULL, argv, environ) == 0 && waitpid(pid, &st, 0) > 0 &&
                WIFEXITED(st) && WEXITSTATUS(st) == 0) {
                state = INST_DONE;
                snprintf(msg, sizeof(msg),
                         "MAC48GX %s was downloaded and verified, and the disk image is open.\n"
                         "Drag MAC48GX.app to Applications, then quit and reopen MAC48GX.", tag);
            } else {
                snprintf(msg, sizeof(msg), "The verified disk image is at\n%s\nOpen it from Finder.", path);
            }
        } else {
            snprintf(msg, sizeof(msg), "%s", err);
        }
    }
    free(json);
    pthread_mutex_lock(&upd_lock);
    inst_state = state;
    snprintf(inst_msg, sizeof(inst_msg), "%s", msg);
    pthread_mutex_unlock(&upd_lock);
    return NULL;
}

void update_install_start(void)
{
    pthread_mutex_lock(&upd_lock);
    if (inst_state == INST_RUNNING) { pthread_mutex_unlock(&upd_lock); return; }
    inst_state = INST_RUNNING;
    pthread_mutex_unlock(&upd_lock);
    pthread_t t;
    if (pthread_create(&t, NULL, inst_thread, NULL) == 0) {
        pthread_detach(t);
    } else {
        pthread_mutex_lock(&upd_lock);
        inst_state = INST_FAILED;
        snprintf(inst_msg, sizeof(inst_msg), "Couldn't start the download.");
        pthread_mutex_unlock(&upd_lock);
    }
}

int update_install_poll(char *message, int message_len)
{
    pthread_mutex_lock(&upd_lock);
    int s = inst_state;
    if (s == INST_DONE || s == INST_FAILED) {
        snprintf(message, message_len, "%s", inst_msg);
        inst_state = INST_IDLE;
    }
    pthread_mutex_unlock(&upd_lock);
    return s;
}

/* ---------------- check-at-startup preference ---------------- */

static void pref_path(char *out, size_t len)
{
    const char *home = getenv("HOME");
    snprintf(out, len, "%s/.droid48/check-updates-at-startup", home ? home : ".");
}

int update_check_at_startup_enabled(void)
{
    char p[1024];
    pref_path(p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return 1;
    int c = fgetc(f);
    fclose(f);
    return c != '0';
}

void update_set_check_at_startup(int on)
{
    char p[1024];
    pref_path(p, sizeof(p));
    FILE *f = fopen(p, "w");
    if (!f) return;
    fputs(on ? "1\n" : "0\n", f);
    fclose(f);
}
