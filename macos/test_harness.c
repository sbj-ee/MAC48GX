/*
 * test_harness.c — Headless test runner for droid48-mac HP-48 emulator.
 *
 * Build:  make test       (from macos/ directory)
 * Run:    build/droid48-test
 *
 * Does NOT require SDL2 — links only against the emulator core + pthread.
 * For visual test mode with the calculator GUI, use: ./droid48-mac --test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <pwd.h>

#include "global.h"
#include "binio.h"
#include "hp48_emu.h"
#include "hp48.h"
#include "debugger.h"
#include "x48.h"
#include "rpl.h"

/* --- Globals required by the emulator core --- */

char  *progname    = "droid48-test";
char  *res_name    = "droid48";
char  *res_class   = "Droid48";
int    saved_argc  = 0;
char **saved_argv  = NULL;
saturn_t saturn;
int      nb;
int      exit_state = 1;
Display *dpy    = NULL;
int      screen = 0;
disp_t   disp;
color_t *colors = NULL;

/* --- Timing --- */

pthread_cond_t  uiConditionVariable = PTHREAD_COND_INITIALIZER;
pthread_mutex_t uiConditionMutex    = PTHREAD_MUTEX_INITIALIZER;
extern int got_alarm;

static void sigalrm_handler(int sig)
{
    (void)sig;
    got_alarm = 1;
}

void blockConditionVariable(void)
{
    struct timeval  now;
    struct timespec ts;
    gettimeofday(&now, NULL);
    ts.tv_sec  = now.tv_sec;
    ts.tv_nsec = now.tv_usec * 1000 + 20000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_timedwait(&uiConditionVariable, &uiConditionMutex, &ts);
    pthread_mutex_unlock(&uiConditionMutex);
}

/* --- File paths (test-specific directory) --- */

char files_path    [256];
char rom_filename  [256];
char ram_filename  [256];
char conf_filename [256];
char port1_filename[256];
char port2_filename[256];

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"), *out;
    char buf[4096]; size_t n;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    return 0;
}

static void setup_test_dir(const char *exe_dir)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char test_dir[256];
    snprintf(test_dir, sizeof(test_dir), "%s/.droid48-test", home);
    mkdir(test_dir, 0755);

    snprintf(files_path, 256, "%s/", test_dir);
    strncpy(rom_filename,   "rom",   255);
    strncpy(ram_filename,   "ram",   255);
    strncpy(conf_filename,  "hp48",  255);
    strncpy(port1_filename, "port1", 255);
    strncpy(port2_filename, "port2", 255);

    char full_rom[256];
    struct stat st;
    snprintf(full_rom, 256, "%s/rom", test_dir);
    if (stat(full_rom, &st) != 0) {
        char src[256];
        snprintf(src, 256, "%s/rom", exe_dir);
        if (stat(src, &st) == 0) copy_file(src, full_rom);
    }
}

/* --- Emulator thread --- */

static volatile int emu_running = 0;

static void *emulator_thread(void *arg)
{
    long flags;
    (void)arg;
    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    flags &= ~O_NDELAY; flags &= ~O_NONBLOCK;
    fcntl(STDIN_FILENO, F_SETFL, flags);

    init_emulator();
    init_active_stuff();
    emu_running = 1;

    do {
        if (!exec_flags) emulate(); else emulate_debug();
        debug();
    } while (exit_state);
    return NULL;
}

/* --- Include test cases --- */
#include "test_cases.h"

/* --- Main --- */

int main(int argc, char **argv)
{
    pthread_t emu_thread;
    struct sigaction sa;
    struct itimerval it;
    char exe_dir[256];

    printf("droid48-mac test suite (headless)\n");
    printf("=================================\n");

    strncpy(exe_dir, argv[0], 255); exe_dir[255] = '\0';
    { char *p = strrchr(exe_dir, '/'); if (p) *p = '\0'; else strcpy(exe_dir, "."); }

    setup_test_dir(exe_dir);

    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigalrm_handler;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);
    it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 20000;
    it.it_value.tv_sec    = 0; it.it_value.tv_usec    = 20000;
    setitimer(ITIMER_REAL, &it, NULL);

    printf("Booting emulator...\n");
    pthread_create(&emu_thread, NULL, emulator_thread, NULL);
    while (!emu_running) usleep(10000);
    usleep(500000);
    printf("Emulator ready.\n");

    run_all_tests();

    exit_state = 0;
    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_broadcast(&uiConditionVariable);
    pthread_mutex_unlock(&uiConditionMutex);
    pthread_join(emu_thread, NULL);

    return tests_failed > 0 ? 1 : 0;
}
