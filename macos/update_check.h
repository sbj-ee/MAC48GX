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

/* Compare two versions like "v1.2.3" / "1.2". Returns >0 if a newer than b. */
int version_compare(const char *a, const char *b);

#endif
