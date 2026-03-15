/*
 * test_harness.c — Automated test suite for droid48-mac HP-48 emulator.
 *
 * Boots the emulator in a background thread, sends button presses via
 * the event queue, reads results from the RPL stack, and compares to
 * expected values.
 *
 * Build:  make test       (from macos/ directory)
 * Run:    build/droid48-test
 *
 * Does NOT require SDL2 — links only against the emulator core + pthread.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
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

/* ------------------------------------------------------------------
 * Globals required by the emulator core
 * ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------
 * Timing
 * ------------------------------------------------------------------ */

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
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_timedwait(&uiConditionVariable, &uiConditionMutex, &ts);
    pthread_mutex_unlock(&uiConditionMutex);
}

/* ------------------------------------------------------------------
 * File paths — use a test-specific directory
 * ------------------------------------------------------------------ */

char files_path    [256];
char rom_filename  [256];
char ram_filename  [256];
char conf_filename [256];
char port1_filename[256];
char port2_filename[256];

static char test_dir[256];

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[4096];
    size_t n;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 0;
}

static void setup_test_dir(const char *exe_dir)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    snprintf(test_dir, sizeof(test_dir), "%s/.droid48-test", home);
    mkdir(test_dir, 0755);

    snprintf(files_path, 256, "%s/", test_dir);
    strncpy(rom_filename,   "rom",   255);
    strncpy(ram_filename,   "ram",   255);
    strncpy(conf_filename,  "hp48",  255);
    strncpy(port1_filename, "port1", 255);
    strncpy(port2_filename, "port2", 255);

    /* Copy ROM from exe dir or jni dir */
    char full_rom[256];
    snprintf(full_rom, 256, "%s/rom", test_dir);
    struct stat st;
    if (stat(full_rom, &st) != 0) {
        char src[256];
        snprintf(src, 256, "%s/rom", exe_dir);
        if (stat(src, &st) == 0) {
            copy_file(src, full_rom);
        }
    }

    /* Keep existing state files for faster subsequent runs.
     * Only a fresh first run needs to go through boot messages. */
}

/* ------------------------------------------------------------------
 * Emulator thread
 * ------------------------------------------------------------------ */

static volatile int emu_running = 0;

static void *emulator_thread(void *arg)
{
    long flags;
    (void)arg;

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    flags &= ~O_NDELAY;
    flags &= ~O_NONBLOCK;
    fcntl(STDIN_FILENO, F_SETFL, flags);

    init_emulator();
    init_active_stuff();
    emu_running = 1;

    do {
        if (!exec_flags)
            emulate();
        else
            emulate_debug();
        debug();
    } while (exit_state);

    return NULL;
}

/* ------------------------------------------------------------------
 * Test helpers
 * ------------------------------------------------------------------ */

extern void sdl_push_event(unsigned int code);
extern unsigned int opt_gx;

/* Wait until emulator thread is running */
static void wait_emu_ready(void)
{
    while (!emu_running)
        usleep(10000);
    /* Give it extra time to boot and reach SHUTDN */
    usleep(500000);
}

/* Press and release a button */
static void press_key(int button_index)
{
    sdl_push_event(button_index + 1);   /* press */
    got_alarm = 1;
    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_signal(&uiConditionVariable);
    pthread_mutex_unlock(&uiConditionMutex);

    usleep(100000);  /* hold 100ms */

    sdl_push_event(button_index + 100); /* release */
    got_alarm = 1;
    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_signal(&uiConditionVariable);
    pthread_mutex_unlock(&uiConditionMutex);

    usleep(60000);   /* inter-key gap 60ms */
}

/* Wait for calculator to finish processing */
static void wait_computation(int ms)
{
    usleep(ms * 1000);
}

/* Button index constants */
#define BTN_0      45
#define BTN_1      40
#define BTN_2      41
#define BTN_3      42
#define BTN_4      35
#define BTN_5      36
#define BTN_6      37
#define BTN_7      30
#define BTN_8      31
#define BTN_9      32
#define BTN_PERIOD 46
#define BTN_SPC    47
#define BTN_PLUS   48
#define BTN_MINUS  43
#define BTN_MUL    38
#define BTN_DIV    33
#define BTN_ENTER  24
#define BTN_NEG    25
#define BTN_EEX    26
#define BTN_DEL    27
#define BTN_BS     28
#define BTN_SIN    18
#define BTN_COS    19
#define BTN_TAN    20
#define BTN_SQRT   21
#define BTN_POWER  22
#define BTN_INV    23
#define BTN_ON     44
#define BTN_LSHIFT 34
#define BTN_RSHIFT 39
#define BTN_ALPHA  29
#define BTN_A      0
#define BTN_B      1
#define BTN_C      2
#define BTN_D      3
#define BTN_E      4
#define BTN_F      5
#define BTN_UP     10
#define BTN_DOWN   16
#define BTN_LEFT   15
#define BTN_RIGHT  17
#define BTN_NXT    11

/* Type a number like "3.14" by pressing digit/period buttons */
static void type_number(const char *num)
{
    const char *p;
    for (p = num; *p; p++) {
        switch (*p) {
        case '0': press_key(BTN_0); break;
        case '1': press_key(BTN_1); break;
        case '2': press_key(BTN_2); break;
        case '3': press_key(BTN_3); break;
        case '4': press_key(BTN_4); break;
        case '5': press_key(BTN_5); break;
        case '6': press_key(BTN_6); break;
        case '7': press_key(BTN_7); break;
        case '8': press_key(BTN_8); break;
        case '9': press_key(BTN_9); break;
        case '.': press_key(BTN_PERIOD); break;
        case '-': press_key(BTN_NEG); break;  /* change sign */
        }
    }
}

/* Read the RPL stack level N (1 = top).
 * Returns the decoded string in buf. Returns 0 on success, -1 on failure. */
static int read_stack_level(int level, char *buf, int bufsz)
{
    word_20 dsktop, dskbot;
    word_20 sp = 0, end = 0, ent = 0;
    word_20 ram_base, ram_mask;
    int n;

    buf[0] = '\0';

    /* Save and reconfigure RAM mapping */
    ram_base = saturn.mem_cntl[1].config[0];
    ram_mask = saturn.mem_cntl[1].config[1];

    if (opt_gx) {
        saturn.mem_cntl[1].config[0] = 0x80000;
        saturn.mem_cntl[1].config[1] = 0xc0000;
        dsktop = 0x806F8;
        dskbot = 0x806FD;
    } else {
        saturn.mem_cntl[1].config[0] = 0x70000;
        saturn.mem_cntl[1].config[1] = 0xf0000;
        dsktop = 0x70579;
        dskbot = 0x7057E;
    }

    load_addr(&sp, dsktop, 5);
    load_addr(&end, dskbot, 5);

    /* Walk the stack to find the Nth entry */
    n = 0;
    word_20 entries[64];
    do {
        load_addr(&ent, sp, 5);
        if (ent == 0) break;
        if (n < 64) entries[n] = ent;
        n++;
        sp += 5;
    } while (sp <= end);

    /* Restore RAM mapping */
    saturn.mem_cntl[1].config[0] = ram_base;
    saturn.mem_cntl[1].config[1] = ram_mask;

    if (level < 1 || level > n)
        return -1;

    /* entries[0] is the top of stack (level 1).
     * entries[1] is level 2, etc. */
    word_20 obj_addr = entries[level - 1];

    /* Temporarily remap RAM again for decode */
    saturn.mem_cntl[1].config[0] = opt_gx ? 0x80000 : 0x70000;
    saturn.mem_cntl[1].config[1] = opt_gx ? 0xc0000 : 0xf0000;

    {
        char typ[256], dat[65536];
        decode_rpl_obj_2(obj_addr, typ, dat);
        /* dat contains just the value (e.g. "5." or "3.14159265359") */
        strncpy(buf, dat, bufsz - 1);
        buf[bufsz - 1] = '\0';
    }

    saturn.mem_cntl[1].config[0] = ram_base;
    saturn.mem_cntl[1].config[1] = ram_mask;

    return 0;
}

/* Clear the stack by pressing ON, then clearing entries */
static void clear_stack(void)
{
    /* Press ON to wake / cancel any pending operation */
    press_key(BTN_ON);
    wait_computation(200);
    /* CLEAR: left-shift + DEL */
    press_key(BTN_LSHIFT);
    press_key(BTN_DEL);
    wait_computation(300);
}

/* Dismiss "Memory Clear" / "Try To Recover Memory?" on fresh boot */
static void dismiss_boot_messages(void)
{
    /* Press ON to dismiss any boot message */
    press_key(BTN_ON);
    wait_computation(500);
    /* Press ON again in case of "Try To Recover Memory?" */
    press_key(BTN_ON);
    wait_computation(500);
    /* Press a menu key to dismiss "Memory Clear" */
    press_key(BTN_F);   /* Usually "NO" or last menu key */
    wait_computation(500);
    press_key(BTN_ON);
    wait_computation(300);
    /* Clear stack */
    clear_stack();
}

/* ------------------------------------------------------------------
 * Test framework
 * ------------------------------------------------------------------ */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Compare stack level 1 value to expected string.
 * The decoded string from decode_rpl_obj includes type prefix like
 * "Real\t<value>" — we check if the expected value appears in the result.
 */
/* Trim leading/trailing whitespace from a string in-place, return pointer */
static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n')) *end-- = '\0';
    return s;
}

static int check_result(const char *test_name, const char *expected)
{
    char buf[65536];
    int rc;

    tests_run++;
    wait_computation(800);  /* let computation finish */

    rc = read_stack_level(1, buf, sizeof(buf));
    if (rc != 0) {
        printf("  FAIL: %s — could not read stack (empty?)\n", test_name);
        tests_failed++;
        return -1;
    }

    char *val = trim(buf);

    /* Exact match or substring match */
    if (strcmp(val, expected) == 0 || strstr(val, expected)) {
        printf("  PASS: %s = %s\n", test_name, val);
        tests_passed++;
        return 0;
    } else {
        printf("  FAIL: %s — expected \"%s\", got \"%s\"\n",
               test_name, expected, val);
        tests_failed++;
        return -1;
    }
}

/* Drop stack level 1: press backspace when command line is empty */
static void drop(void)
{
    press_key(BTN_BS);
    wait_computation(300);
}

/* ------------------------------------------------------------------
 * Test cases
 * ------------------------------------------------------------------ */

static void test_basic_arithmetic(void)
{
    printf("\n--- Basic Arithmetic ---\n");

    /* 2 + 3 = 5 */
    type_number("2");
    press_key(BTN_ENTER);
    type_number("3");
    press_key(BTN_PLUS);
    check_result("2 + 3", "5");
    drop();

    /* 10 - 3 = 7 */
    type_number("10");
    press_key(BTN_ENTER);
    type_number("3");
    press_key(BTN_MINUS);
    check_result("10 - 3", "7");
    drop();

    /* 6 * 7 = 42 */
    type_number("6");
    press_key(BTN_ENTER);
    type_number("7");
    press_key(BTN_MUL);
    check_result("6 * 7", "42");
    drop();

    /* 100 / 4 = 25 */
    type_number("100");
    press_key(BTN_ENTER);
    type_number("4");
    press_key(BTN_DIV);
    check_result("100 / 4", "25");
    drop();

    /* 100 / 3 */
    type_number("100");
    press_key(BTN_ENTER);
    type_number("3");
    press_key(BTN_DIV);
    check_result("100 / 3", "33.3333333333");
    drop();
}

static void test_decimal_numbers(void)
{
    printf("\n--- Decimal Numbers ---\n");

    /* 3.14 entered correctly */
    type_number("3.14");
    press_key(BTN_ENTER);
    check_result("enter 3.14", "3.14");
    drop();

    /* 0.5 + 0.25 = 0.75 */
    type_number("0.5");
    press_key(BTN_ENTER);
    type_number("0.25");
    press_key(BTN_PLUS);
    check_result("0.5 + 0.25", ".75");
    drop();
}

static void test_negative_numbers(void)
{
    printf("\n--- Negative Numbers ---\n");

    /* -5 + 3 = -2 */
    type_number("5");
    press_key(BTN_NEG);  /* +/- to negate */
    press_key(BTN_ENTER);
    type_number("3");
    press_key(BTN_PLUS);
    check_result("-5 + 3", "-2");
    drop();
}

static void test_powers_and_roots(void)
{
    printf("\n--- Powers and Roots ---\n");

    /* 2^3 = 8 */
    type_number("2");
    press_key(BTN_ENTER);
    type_number("3");
    press_key(BTN_POWER);
    check_result("2^3", "8");
    drop();

    /* sqrt(9) = 3 */
    type_number("9");
    press_key(BTN_SQRT);
    check_result("sqrt(9)", "3");
    drop();

    /* sqrt(2) */
    type_number("2");
    press_key(BTN_SQRT);
    check_result("sqrt(2)", "1.41421356237");
    drop();

    /* 1/4 = 0.25 */
    type_number("4");
    press_key(BTN_INV);
    check_result("1/4", ".25");
    drop();
}

static void test_trigonometry(void)
{
    printf("\n--- Trigonometry (DEG mode, default) ---\n");

    /* sin(0) = 0 */
    type_number("0");
    press_key(BTN_SIN);
    check_result("sin(0)", "0");
    drop();

    /* sin(90) = 1 */
    type_number("90");
    press_key(BTN_SIN);
    check_result("sin(90)", "1");
    drop();

    /* sin(30) = 0.5 */
    type_number("30");
    press_key(BTN_SIN);
    check_result("sin(30)", ".5");
    drop();

    /* cos(0) = 1 */
    type_number("0");
    press_key(BTN_COS);
    check_result("cos(0)", "1");
    drop();

    /* cos(60) = 0.5 */
    type_number("60");
    press_key(BTN_COS);
    check_result("cos(60)", ".5");
    drop();

    /* tan(45) = 1 */
    type_number("45");
    press_key(BTN_TAN);
    check_result("tan(45)", "1");
    drop();
}

static void test_stack_operations(void)
{
    printf("\n--- Stack Operations ---\n");
    char buf[65536];

    /* Push 1, 2, 3 — check all three levels */
    type_number("1");
    press_key(BTN_ENTER);
    type_number("2");
    press_key(BTN_ENTER);
    type_number("3");
    press_key(BTN_ENTER);
    wait_computation(500);

    read_stack_level(1, buf, sizeof(buf));
    tests_run++;
    if (strstr(buf, "3")) {
        printf("  PASS: stack level 1 = 3\n");
        tests_passed++;
    } else {
        printf("  FAIL: stack level 1 — expected 3, got \"%s\"\n", buf);
        tests_failed++;
    }

    read_stack_level(2, buf, sizeof(buf));
    tests_run++;
    if (strstr(buf, "2")) {
        printf("  PASS: stack level 2 = 2\n");
        tests_passed++;
    } else {
        printf("  FAIL: stack level 2 — expected 2, got \"%s\"\n", buf);
        tests_failed++;
    }

    read_stack_level(3, buf, sizeof(buf));
    tests_run++;
    if (strstr(buf, "1")) {
        printf("  PASS: stack level 3 = 1\n");
        tests_passed++;
    } else {
        printf("  FAIL: stack level 3 — expected 1, got \"%s\"\n", buf);
        tests_failed++;
    }

    /* Clean up */
    drop(); drop(); drop();
}

static void test_larger_computations(void)
{
    printf("\n--- Larger Computations ---\n");

    /* 12345 + 67890 = 80235 */
    type_number("12345");
    press_key(BTN_ENTER);
    type_number("67890");
    press_key(BTN_PLUS);
    check_result("12345 + 67890", "80235");
    drop();

    /* 2^10 = 1024 */
    type_number("2");
    press_key(BTN_ENTER);
    type_number("10");
    press_key(BTN_POWER);
    check_result("2^10", "1024");
    drop();

    /* 999 * 999 = 998001 */
    type_number("999");
    press_key(BTN_ENTER);
    type_number("999");
    press_key(BTN_MUL);
    check_result("999 * 999", "998001");
    drop();
}

static void test_chained_operations(void)
{
    printf("\n--- Chained Operations ---\n");

    /* (2 + 3) * 4 = 20 */
    type_number("2");
    press_key(BTN_ENTER);
    type_number("3");
    press_key(BTN_PLUS);
    type_number("4");
    press_key(BTN_MUL);
    check_result("(2+3)*4", "20");
    drop();

    /* sqrt(3^2 + 4^2) = 5 (Pythagorean) */
    type_number("3");
    press_key(BTN_ENTER);
    type_number("2");
    press_key(BTN_POWER);
    type_number("4");
    press_key(BTN_ENTER);
    type_number("2");
    press_key(BTN_POWER);
    press_key(BTN_PLUS);
    press_key(BTN_SQRT);
    check_result("sqrt(3^2+4^2)", "5");
    drop();
}

/* ------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    pthread_t emu_thread;
    struct sigaction sa;
    struct itimerval it;
    char exe_dir[256];

    printf("droid48-mac test suite\n");
    printf("======================\n");

    /* Determine exe directory */
    {
        char *last_slash;
        strncpy(exe_dir, argv[0], 255);
        exe_dir[255] = '\0';
        last_slash = strrchr(exe_dir, '/');
        if (last_slash)
            *last_slash = '\0';
        else
            strcpy(exe_dir, ".");
    }

    /* Setup */
    setup_test_dir(exe_dir);

    /* SIGALRM */
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigalrm_handler;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    it.it_interval.tv_sec  = 0;
    it.it_interval.tv_usec = 20000;
    it.it_value.tv_sec     = 0;
    it.it_value.tv_usec    = 20000;
    setitimer(ITIMER_REAL, &it, NULL);

    /* Start emulator */
    printf("Booting emulator...\n");
    if (pthread_create(&emu_thread, NULL, emulator_thread, NULL) != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(errno));
        return 1;
    }

    wait_emu_ready();
    printf("Emulator ready.\n");

    /* Dismiss boot messages and clear stack */
    dismiss_boot_messages();
    printf("Boot messages dismissed, running tests...\n");

    /* Run test suites — clear stack between groups */
    test_basic_arithmetic();
    clear_stack();
    test_decimal_numbers();
    clear_stack();
    test_negative_numbers();
    clear_stack();
    test_powers_and_roots();
    clear_stack();
    test_trigonometry();
    clear_stack();
    test_stack_operations();
    clear_stack();
    test_larger_computations();
    clear_stack();
    test_chained_operations();

    /* Summary */
    printf("\n======================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);

    /* Shutdown */
    exit_state = 0;
    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_broadcast(&uiConditionVariable);
    pthread_mutex_unlock(&uiConditionMutex);
    pthread_join(emu_thread, NULL);

    return tests_failed > 0 ? 1 : 0;
}
