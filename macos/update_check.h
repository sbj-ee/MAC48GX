/* update_check.h — Check GitHub for a newer MAC48GX release. */
#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

enum {
    UPD_IDLE = 0,     /* nothing in flight / result consumed */
    UPD_RUNNING,      /* background fetch in progress */
    UPD_NEWER,        /* newer release available */
    UPD_CURRENT,      /* already on latest */
    UPD_FAILED        /* network / parse error */
};

/* Start a background check. silent=1 means only report if newer. */
void update_check_start(int silent);

/* Poll from the UI thread; returns UPD_* and fills tag/url when done.
 * Returns UPD_IDLE for silent checks that found nothing to report. */
int update_check_poll(char *tag, int tag_len, char *url, int url_len);

/* ---- Install (zwriter's flow): download SHA256SUMS + SHA256SUMS.minisig,
 * verify the minisign signature with the embedded key, download the .dmg,
 * check its SHA-256, then open it so MAC48GX.app can be dragged to
 * Applications. Call after update_check_poll() returned UPD_NEWER. */
enum {
    INST_IDLE = 0,
    INST_RUNNING,
    INST_DONE,        /* verified and opened; message has the path */
    INST_FAILED       /* message says why; nothing was opened */
};
void update_install_start(void);
int update_install_poll(char *message, int message_len);

/* Run the verified download in the calling thread (the tests use this with
 * a fake fetcher). fetch(url, dest_path) returns 0 on success. dir must
 * exist. On success *out_path is the verified .dmg. */
typedef int (*update_fetch_fn)(const char *url, const char *dest_path);
int update_download_verified(const char *release_json, const char *tag, const char *dir,
                             const char *pubkey_b64, update_fetch_fn fetch,
                             char *out_path, int out_len, char *err, int err_len);

/* Check-at-startup preference: ~/.droid48/check-updates-at-startup ("0" off).
 * Default on. Toggled with Cmd+Shift+U. */
int update_check_at_startup_enabled(void);
void update_set_check_at_startup(int on);

/* Compare two versions like "v1.2.3" / "1.2". Returns >0 if a newer than b. */
int version_compare(const char *a, const char *b);

#endif
