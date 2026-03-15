/*
 * test_cases.h — Shared test functions for droid48-mac.
 *
 * Used by both test_harness.c (headless) and main_sdl.c (--test GUI mode).
 * All functions assume the emulator is already running in a background thread.
 */

#ifndef TEST_CASES_H
#define TEST_CASES_H

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "global.h"
#include "hp48.h"
#include "hp48_emu.h"
#include "x48.h"
#include "rpl.h"

/* --- Externs from emulator --- */
extern saturn_t saturn;
extern int got_alarm;
extern unsigned int opt_gx;
extern void sdl_push_event(unsigned int code);
extern void load_addr(word_20 *dat, long addr, int n);
extern void decode_rpl_obj_2(word_20 addr, char *typ, char *dat);
extern pthread_cond_t  uiConditionVariable;
extern pthread_mutex_t uiConditionMutex;

/* --- Button constants --- */
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

/* --- Test counters --- */
static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* --- Helpers --- */

static void press_key(int button_index)
{
    sdl_push_event(button_index + 1);
    got_alarm = 1;
    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_signal(&uiConditionVariable);
    pthread_mutex_unlock(&uiConditionMutex);
    usleep(100000);

    sdl_push_event(button_index + 100);
    got_alarm = 1;
    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_signal(&uiConditionVariable);
    pthread_mutex_unlock(&uiConditionMutex);
    usleep(60000);
}

static void wait_computation(int ms) { usleep(ms * 1000); }

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
        case '-': press_key(BTN_NEG); break;
        }
    }
}

static int read_stack_level(int level, char *buf, int bufsz)
{
    word_20 dsktop, dskbot;
    word_20 sp = 0, end = 0, ent = 0;
    word_20 ram_base, ram_mask;
    int n;

    buf[0] = '\0';
    ram_base = saturn.mem_cntl[1].config[0];
    ram_mask = saturn.mem_cntl[1].config[1];

    if (opt_gx) {
        saturn.mem_cntl[1].config[0] = 0x80000;
        saturn.mem_cntl[1].config[1] = 0xc0000;
        dsktop = 0x806F8; dskbot = 0x806FD;
    } else {
        saturn.mem_cntl[1].config[0] = 0x70000;
        saturn.mem_cntl[1].config[1] = 0xf0000;
        dsktop = 0x70579; dskbot = 0x7057E;
    }

    load_addr(&sp, dsktop, 5);
    load_addr(&end, dskbot, 5);

    n = 0;
    word_20 entries[64];
    do {
        load_addr(&ent, sp, 5);
        if (ent == 0) break;
        if (n < 64) entries[n] = ent;
        n++;
        sp += 5;
    } while (sp <= end);

    saturn.mem_cntl[1].config[0] = ram_base;
    saturn.mem_cntl[1].config[1] = ram_mask;

    if (level < 1 || level > n) return -1;

    word_20 obj_addr = entries[level - 1];

    saturn.mem_cntl[1].config[0] = opt_gx ? 0x80000 : 0x70000;
    saturn.mem_cntl[1].config[1] = opt_gx ? 0xc0000 : 0xf0000;
    {
        char typ[256], dat[65536];
        decode_rpl_obj_2(obj_addr, typ, dat);
        strncpy(buf, dat, bufsz - 1);
        buf[bufsz - 1] = '\0';
    }
    saturn.mem_cntl[1].config[0] = ram_base;
    saturn.mem_cntl[1].config[1] = ram_mask;
    return 0;
}

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
    tests_run++;
    wait_computation(800);

    if (read_stack_level(1, buf, sizeof(buf)) != 0) {
        printf("  FAIL: %s — could not read stack\n", test_name);
        tests_failed++;
        return -1;
    }
    char *val = trim(buf);
    if (strcmp(val, expected) == 0 || strstr(val, expected)) {
        printf("  PASS: %s = %s\n", test_name, val);
        tests_passed++;
        return 0;
    } else {
        printf("  FAIL: %s — expected \"%s\", got \"%s\"\n", test_name, expected, val);
        tests_failed++;
        return -1;
    }
}

static void drop(void) { press_key(BTN_BS); wait_computation(300); }

static void clear_stack(void)
{
    press_key(BTN_ON);
    wait_computation(200);
    press_key(BTN_LSHIFT);
    press_key(BTN_DEL);
    wait_computation(300);
}

/* --- Test suites --- */

static void test_basic_arithmetic(void)
{
    printf("\n--- Basic Arithmetic ---\n");
    type_number("2"); press_key(BTN_ENTER); type_number("3"); press_key(BTN_PLUS);
    check_result("2 + 3", "5"); drop();

    type_number("10"); press_key(BTN_ENTER); type_number("3"); press_key(BTN_MINUS);
    check_result("10 - 3", "7"); drop();

    type_number("6"); press_key(BTN_ENTER); type_number("7"); press_key(BTN_MUL);
    check_result("6 * 7", "42"); drop();

    type_number("100"); press_key(BTN_ENTER); type_number("4"); press_key(BTN_DIV);
    check_result("100 / 4", "25"); drop();

    type_number("100"); press_key(BTN_ENTER); type_number("3"); press_key(BTN_DIV);
    check_result("100 / 3", "33.3333333333"); drop();
}

static void test_decimal_numbers(void)
{
    printf("\n--- Decimal Numbers ---\n");
    type_number("3.14"); press_key(BTN_ENTER);
    check_result("enter 3.14", "3.14"); drop();

    type_number("0.5"); press_key(BTN_ENTER); type_number("0.25"); press_key(BTN_PLUS);
    check_result("0.5 + 0.25", ".75"); drop();
}

static void test_negative_numbers(void)
{
    printf("\n--- Negative Numbers ---\n");
    type_number("5"); press_key(BTN_NEG); press_key(BTN_ENTER);
    type_number("3"); press_key(BTN_PLUS);
    check_result("-5 + 3", "-2"); drop();
}

static void test_powers_and_roots(void)
{
    printf("\n--- Powers and Roots ---\n");
    type_number("2"); press_key(BTN_ENTER); type_number("3"); press_key(BTN_POWER);
    check_result("2^3", "8"); drop();

    type_number("9"); press_key(BTN_SQRT);
    check_result("sqrt(9)", "3"); drop();

    type_number("2"); press_key(BTN_SQRT);
    check_result("sqrt(2)", "1.41421356237"); drop();

    type_number("4"); press_key(BTN_INV);
    check_result("1/4", ".25"); drop();
}

static void test_trigonometry(void)
{
    printf("\n--- Trigonometry (DEG mode) ---\n");
    type_number("0"); press_key(BTN_SIN);  check_result("sin(0)", "0");   drop();
    type_number("90"); press_key(BTN_SIN); check_result("sin(90)", "1");  drop();
    type_number("30"); press_key(BTN_SIN); check_result("sin(30)", ".5"); drop();
    type_number("0"); press_key(BTN_COS);  check_result("cos(0)", "1");   drop();
    type_number("60"); press_key(BTN_COS); check_result("cos(60)", ".5"); drop();
    type_number("45"); press_key(BTN_TAN); check_result("tan(45)", "1");  drop();
}

static void test_stack_operations(void)
{
    printf("\n--- Stack Operations ---\n");
    char buf[65536];
    type_number("1"); press_key(BTN_ENTER);
    type_number("2"); press_key(BTN_ENTER);
    type_number("3"); press_key(BTN_ENTER);
    wait_computation(500);

    tests_run++;
    read_stack_level(1, buf, sizeof(buf));
    if (strstr(buf, "3")) { printf("  PASS: stack level 1 = 3\n"); tests_passed++; }
    else { printf("  FAIL: stack level 1 — expected 3, got \"%s\"\n", buf); tests_failed++; }

    tests_run++;
    read_stack_level(2, buf, sizeof(buf));
    if (strstr(buf, "2")) { printf("  PASS: stack level 2 = 2\n"); tests_passed++; }
    else { printf("  FAIL: stack level 2 — expected 2, got \"%s\"\n", buf); tests_failed++; }

    tests_run++;
    read_stack_level(3, buf, sizeof(buf));
    if (strstr(buf, "1")) { printf("  PASS: stack level 3 = 1\n"); tests_passed++; }
    else { printf("  FAIL: stack level 3 — expected 1, got \"%s\"\n", buf); tests_failed++; }

    drop(); drop(); drop();
}

static void test_larger_computations(void)
{
    printf("\n--- Larger Computations ---\n");
    type_number("12345"); press_key(BTN_ENTER); type_number("67890"); press_key(BTN_PLUS);
    check_result("12345 + 67890", "80235"); drop();

    type_number("2"); press_key(BTN_ENTER); type_number("10"); press_key(BTN_POWER);
    check_result("2^10", "1024"); drop();

    type_number("999"); press_key(BTN_ENTER); type_number("999"); press_key(BTN_MUL);
    check_result("999 * 999", "998001"); drop();
}

static void test_chained_operations(void)
{
    printf("\n--- Chained Operations ---\n");
    type_number("2"); press_key(BTN_ENTER); type_number("3"); press_key(BTN_PLUS);
    type_number("4"); press_key(BTN_MUL);
    check_result("(2+3)*4", "20"); drop();

    type_number("3"); press_key(BTN_ENTER); type_number("2"); press_key(BTN_POWER);
    type_number("4"); press_key(BTN_ENTER); type_number("2"); press_key(BTN_POWER);
    press_key(BTN_PLUS); press_key(BTN_SQRT);
    check_result("sqrt(3^2+4^2)", "5"); drop();
}

/* --- Run all tests --- */
static void run_all_tests(void)
{
    tests_run = tests_passed = tests_failed = 0;

    /* Dismiss boot messages */
    press_key(BTN_ON);   wait_computation(500);
    press_key(BTN_ON);   wait_computation(500);
    press_key(BTN_F);    wait_computation(500);
    press_key(BTN_ON);   wait_computation(300);
    clear_stack();

    test_basic_arithmetic();   clear_stack();
    test_decimal_numbers();    clear_stack();
    test_negative_numbers();   clear_stack();
    test_powers_and_roots();   clear_stack();
    test_trigonometry();       clear_stack();
    test_stack_operations();   clear_stack();
    test_larger_computations();clear_stack();
    test_chained_operations();

    printf("\n======================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);
}

#endif /* TEST_CASES_H */
