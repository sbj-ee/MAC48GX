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
#include "update_check.h"

#define RELEASES_API "https://api.github.com/repos/sbj-ee/MAC48GX/releases/latest"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

static pthread_mutex_t upd_lock = PTHREAD_MUTEX_INITIALIZER;
static int  upd_state  = UPD_IDLE;
static int  upd_silent = 0;
static char upd_tag[64];
static char upd_url[512];

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
