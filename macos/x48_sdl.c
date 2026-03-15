/*
 * x48_sdl.c - macOS/SDL2 replacement for x48.c (Android JNI version).
 *
 * Copyright (C) 2026  droid48-mac contributors
 * Based on x48 by Eddie C. Dost (Copyright (C) 1994-2005)
 * and droid48 by Arnaud Brochard (shagr4th)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v3.0 as published
 * by the Free Software Foundation.
 *
 * Provides:
 *  - buttons[] array with HP-48 key codes and layout coordinates
 *  - button_pressed() / button_released() / key_event()
 *  - GetEvent() — non-blocking, pops events from a thread-safe queue
 *  - Stub X11 functions (XCreateBitmapFromData, XClearArea, etc.)
 *  - adjust_contrast(), refresh_icon(), ShowConnections(), exit_x48()
 */

#include "global.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "x48.h"
#include "icon.h"
#include "small.h"
#include "buttons.h"
#include "hp48.h"
#include "device.h"
#include "constants.h"
#include "romio.h"

/* ------------------------------------------------------------------
 * Button table — same as original x48.c
 * ------------------------------------------------------------------ */

typedef struct button_t {
    char         *name;
    short         pressed;
    short         extra;
    int           code;
    int           x, y;
    unsigned int  w, h;
    int           lc;
    char         *label;
    short         font_size;
    unsigned int  lw, lh;
    unsigned char *lb;
    char         *letter;
    char         *left;
    short         is_menu;
    char         *right;
    char         *sub;
    Pixmap        map;
    Pixmap        down;
} button_t;

#define BUTTON_A    0
#define BUTTON_B    1
#define BUTTON_C    2
#define BUTTON_D    3
#define BUTTON_E    4
#define BUTTON_F    5
#define BUTTON_MTH  6
#define BUTTON_PRG  7
#define BUTTON_CST  8
#define BUTTON_VAR  9
#define BUTTON_UP  10
#define BUTTON_NXT 11
#define BUTTON_COLON 12
#define BUTTON_STO  13
#define BUTTON_EVAL 14
#define BUTTON_LEFT 15
#define BUTTON_DOWN 16
#define BUTTON_RIGHT 17
#define BUTTON_SIN  18
#define BUTTON_COS  19
#define BUTTON_TAN  20
#define BUTTON_SQRT 21
#define BUTTON_POWER 22
#define BUTTON_INV  23
#define BUTTON_ENTER 24
#define BUTTON_NEG  25
#define BUTTON_EEX  26
#define BUTTON_DEL  27
#define BUTTON_BS   28
#define BUTTON_ALPHA 29
#define BUTTON_7    30
#define BUTTON_8    31
#define BUTTON_9    32
#define BUTTON_DIV  33
#define BUTTON_SHL  34
#define BUTTON_4    35
#define BUTTON_5    36
#define BUTTON_6    37
#define BUTTON_MUL  38
#define BUTTON_SHR  39
#define BUTTON_1    40
#define BUTTON_2    41
#define BUTTON_3    42
#define BUTTON_MINUS 43
#define BUTTON_ON   44
#define BUTTON_0    45
#define BUTTON_PERIOD 46
#define BUTTON_SPC  47
#define BUTTON_PLUS 48
#define LAST_BUTTON 48

button_t buttons[] = {
  { "A",     0,0, 0x14,   0,  0, 36,23, WHITE, 0,0, menu_label_width,menu_label_height,menu_label_bits, "A",  0,     0, 0,      0, 0 },
  { "B",     0,0, 0x84,  50,  0, 36,23, WHITE, 0,0, menu_label_width,menu_label_height,menu_label_bits, "B",  0,     0, 0,      0, 0 },
  { "C",     0,0, 0x83, 100,  0, 36,23, WHITE, 0,0, menu_label_width,menu_label_height,menu_label_bits, "C",  0,     0, 0,      0, 0 },
  { "D",     0,0, 0x82, 150,  0, 36,23, WHITE, 0,0, menu_label_width,menu_label_height,menu_label_bits, "D",  0,     0, 0,      0, 0 },
  { "E",     0,0, 0x81, 200,  0, 36,23, WHITE, 0,0, menu_label_width,menu_label_height,menu_label_bits, "E",  0,     0, 0,      0, 0 },
  { "F",     0,0, 0x80, 250,  0, 36,23, WHITE, 0,0, menu_label_width,menu_label_height,menu_label_bits, "F",  0,     0, 0,      0, 0 },

  { "MTH",   0,0, 0x24,   0, 50, 36,26, WHITE, "MTH",  0,0,0,0, "G",  "RAD",   0, "POLAR",  0, 0 },
  { "PRG",   0,0, 0x74,  50, 50, 36,26, WHITE, "PRG",  0,0,0,0, "H",  0,       0, "CHARS",  0, 0 },
  { "CST",   0,0, 0x73, 100, 50, 36,26, WHITE, "CST",  0,0,0,0, "I",  0,       0, "MODES",  0, 0 },
  { "VAR",   0,0, 0x72, 150, 50, 36,26, WHITE, "VAR",  0,0,0,0, "J",  0,       0, "MEMORY", 0, 0 },
  { "UP",    0,0, 0x71, 200, 50, 36,26, WHITE, 0,0, up_width,up_height,up_bits, "K", 0, 0, "STACK", 0, 0 },
  { "NXT",   0,0, 0x70, 250, 50, 36,26, WHITE, "NXT",  0,0,0,0, "L",  "PREV",  0, "MENU",   0, 0 },

  { "COLON", 0,0, 0x04,   0,100, 36,26, WHITE, 0,0, colon_width,colon_height,colon_bits, "M", "UP", 0, "HOME", 0, 0 },
  { "STO",   0,0, 0x64,  50,100, 36,26, WHITE, "STO",  0,0,0,0, "N",  "DEF",   0, "RCL",    0, 0 },
  { "EVAL",  0,0, 0x63, 100,100, 36,26, WHITE, "EVAL", 0,0,0,0, "O",  "aNUM",  0, "UNDO",   0, 0 },
  { "LEFT",  0,0, 0x62, 150,100, 36,26, WHITE, 0,0, left_width,left_height,left_bits,   "P", "PICTURE", 0, 0, 0, 0 },
  { "DOWN",  0,0, 0x61, 200,100, 36,26, WHITE, 0,0, down_width,down_height,down_bits,   "Q", "VIEW",    0, 0, 0, 0 },
  { "RIGHT", 0,0, 0x60, 250,100, 36,26, WHITE, 0,0, right_width,right_height,right_bits,"R", "SWAP",    0, 0, 0, 0 },

  { "SIN",   0,0, 0x34,   0,150, 36,26, WHITE, "SIN",  0,0,0,0, "S",  "ASIN",  0, "b",  0, 0 },
  { "COS",   0,0, 0x54,  50,150, 36,26, WHITE, "COS",  0,0,0,0, "T",  "ACOS",  0, "c",  0, 0 },
  { "TAN",   0,0, 0x53, 100,150, 36,26, WHITE, "TAN",  0,0,0,0, "U",  "ATAN",  0, "d",  0, 0 },
  { "SQRT",  0,0, 0x52, 150,150, 36,26, WHITE, 0,0, sqrt_width,sqrt_height,sqrt_bits, "V","n",0,"o",0,0 },
  { "POWER", 0,0, 0x51, 200,150, 36,26, WHITE, 0,0, power_width,power_height,power_bits,"W","p",0,"LOG",0,0 },
  { "INV",   0,0, 0x50, 250,150, 36,26, WHITE, 0,0, inv_width,inv_height,inv_bits, "X","q",0,"LN",0,0 },

  { "ENTER", 0,0, 0x44,   0,200, 86,26, WHITE, "ENTER",2,0,0,0,  0,  "EQUATION",0,"MATRIX",0,0 },
  { "NEG",   0,0, 0x43, 100,200, 36,26, WHITE, 0,0, neg_width,neg_height,neg_bits, "Y","EDIT",0,"CMD",0,0 },
  { "EEX",   0,0, 0x42, 150,200, 36,26, WHITE, "EEX",  0,0,0,0,  "Z","PURG",   0, "ARG",    0, 0 },
  { "DEL",   0,0, 0x41, 200,200, 36,26, WHITE, "DEL",  0,0,0,0,   0, "CLEAR",   0,  0,        0, 0 },
  { "BS",    0,0, 0x40, 250,200, 36,26, WHITE, 0,0, bs_width,bs_height,bs_bits,    0, "DROP",   0,  0,        0, 0 },

  { "ALPHA", 0,0, 0x35,   0,250, 36,26, WHITE, 0,0, alpha_width,alpha_height,alpha_bits, 0,"USER",0,"ENTRY",0,0 },
  { "7",     0,0, 0x33,  60,250, 46,26, WHITE, "7",1,0,0,0,       0,  0,         1, "SOLVE",    0, 0 },
  { "8",     0,0, 0x32, 120,250, 46,26, WHITE, "8",1,0,0,0,       0,  0,         1, "PLOT",     0, 0 },
  { "9",     0,0, 0x31, 180,250, 46,26, WHITE, "9",1,0,0,0,       0,  0,         1, "SYMBOLIC", 0, 0 },
  { "DIV",   0,0, 0x30, 240,250, 46,26, WHITE, 0,0, div_width,div_height,div_bits, 0,"r ",0,"s",0,0 },

  { "SHL",   0,0, 0x25,   0,300, 36,26, LEFT,  0,0, shl_width,shl_height,shl_bits, 0,0,0,0,0,0 },
  { "4",     0,0, 0x23,  60,300, 46,26, WHITE, "4",1,0,0,0,       0,  0,         1, "TIME",     0, 0 },
  { "5",     0,0, 0x22, 120,300, 46,26, WHITE, "5",1,0,0,0,       0,  0,         1, "STAT",     0, 0 },
  { "6",     0,0, 0x21, 180,300, 46,26, WHITE, "6",1,0,0,0,       0,  0,         1, "UNITS",    0, 0 },
  { "MUL",   0,0, 0x20, 240,300, 46,26, WHITE, 0,0, mul_width,mul_height,mul_bits, 0,"t ",0,"u",0,0 },

  { "SHR",   0,0, 0x15,   0,350, 36,26, RIGHT, 0,0, shr_width,shr_height,shr_bits, 0,0,1," ",0,0 },
  { "1",     0,0, 0x13,  60,350, 46,26, WHITE, "1",1,0,0,0,       0,  0,         1, "I/O",      0, 0 },
  { "2",     0,0, 0x12, 120,350, 46,26, WHITE, "2",1,0,0,0,       0,  0,         1, "LIBRARY",  0, 0 },
  { "3",     0,0, 0x11, 180,350, 46,26, WHITE, "3",1,0,0,0,       0,  0,         1, "EQ LIB",   0, 0 },
  { "MINUS", 0,0, 0x10, 240,350, 46,26, WHITE, 0,0, minus_width,minus_height,minus_bits, 0,"v ",0,"w",0,0 },

  { "ON",    0,0, 0x8000, 0,400, 36,26, WHITE, "ON",   0,0,0,0,   0, "CONT",    0, "OFF","CANCEL",0 },
  { "0",     0,0, 0x03,  60,400, 46,26, WHITE, "0",1,0,0,0,       0, "\004 ",   0, "\003",      0, 0 },
  { "PERIOD",0,0, 0x02, 120,400, 46,26, WHITE, ".",1,0,0,0,        0, "\002 ",   0, "\001",      0, 0 },
  { "SPC",   0,0, 0x01, 180,400, 46,26, WHITE, "SPC",  0,0,0,0,   0, "\005 ",   0, "z",         0, 0 },
  { "PLUS",  0,0, 0x00, 240,400, 46,26, WHITE, 0,0, plus_width,plus_height,plus_bits, 0,"x ",0,"y",0,0 },
  { 0 }
};

/* ------------------------------------------------------------------
 * Button press / release — directly updates saturn.keybuf
 * (called from both the SDL event thread and the emulator thread)
 * ------------------------------------------------------------------ */

int button_pressed(int b)
{
    int code = buttons[b].code;
    buttons[b].pressed = 1;
    if (code == 0x8000) {
        int i;
        for (i = 0; i < 9; i++)
            saturn.keybuf.rows[i] |= 0x8000;
        do_kbd_int();
    } else {
        int r = code >> 4;
        int c = 1 << (code & 0xf);
        if ((saturn.keybuf.rows[r] & c) == 0) {
            if (saturn.kbd_ien)
                do_kbd_int();
            saturn.keybuf.rows[r] |= c;
        }
    }
    return 0;
}

int button_released(int b)
{
    int code = buttons[b].code;
    buttons[b].pressed = 0;
    if (code == 0x8000) {
        int i;
        for (i = 0; i < 9; i++)
            saturn.keybuf.rows[i] &= ~0x8000;
    } else {
        int r = code >> 4;
        int c = 1 << (code & 0xf);
        saturn.keybuf.rows[r] &= ~c;
    }
    return 0;
}

static int button_release_all(void)
{
    int b, code;
    for (b = BUTTON_A; b <= LAST_BUTTON; b++) {
        if (buttons[b].pressed) {
            code = buttons[b].code;
            if (code == 0x8000) {
                int i;
                for (i = 0; i < 9; i++)
                    saturn.keybuf.rows[i] &= ~0x8000;
            } else {
                int r = code >> 4;
                int c = 1 << (code & 0xf);
                saturn.keybuf.rows[r] &= ~c;
            }
            buttons[b].pressed = 0;
        }
    }
    return 0;
}

int key_event(int b, int keypressed)
{
    int code = buttons[b].code;
    if (keypressed == 1) {
        buttons[b].pressed = 1;
        if (code == 0x8000) {
            int i;
            for (i = 0; i < 9; i++)
                saturn.keybuf.rows[i] |= 0x8000;
            do_kbd_int();
        } else {
            int r = code >> 4;
            int c = 1 << (code & 0xf);
            if ((saturn.keybuf.rows[r] & c) == 0) {
                if (saturn.kbd_ien)
                    do_kbd_int();
                saturn.keybuf.rows[r] |= c;
            }
        }
    } else {
        if (code == 0x8000) {
            int i;
            for (i = 0; i < 9; i++)
                saturn.keybuf.rows[i] &= ~0x8000;
            memset(&saturn.keybuf, 0, sizeof(saturn.keybuf));
        } else {
            int r = code >> 4;
            int c = 1 << (code & 0xf);
            saturn.keybuf.rows[r] &= ~c;
        }
        buttons[b].pressed = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------
 * Thread-safe event queue for button presses/releases.
 *
 * The SDL main thread pushes events via sdl_push_event().
 * The emulator thread pops them in GetEvent().
 *
 * Event codes (same convention as Android):
 *   1..49   = button press  (button index = code - 1)
 *   100..148 = button release (button index = code - 100)
 * ------------------------------------------------------------------ */

#define EVQ_SIZE 64
static unsigned int evq_buf[EVQ_SIZE];
static int          evq_head = 0;   /* write index (SDL thread)    */
static int          evq_tail = 0;   /* read  index (emulator thread) */
static pthread_mutex_t evq_mutex = PTHREAD_MUTEX_INITIALIZER;

void sdl_push_event(unsigned int code)
{
    pthread_mutex_lock(&evq_mutex);
    evq_buf[evq_head] = code;
    evq_head = (evq_head + 1) % EVQ_SIZE;
    pthread_mutex_unlock(&evq_mutex);
}

/* GetEvent — called from the emulator thread (emulate.c / actions.c).
 * Drains the event queue and calls key_event() for each entry so that
 * do_kbd_int() fires in the emulator's context — which is required
 * for the SHUTDN handler to notice the key and wake the CPU.
 */
int GetEvent(void)
{
    int wake = 0;

    pthread_mutex_lock(&evq_mutex);
    while (evq_head != evq_tail) {
        unsigned int code = evq_buf[evq_tail];
        evq_tail = (evq_tail + 1) % EVQ_SIZE;

        pthread_mutex_unlock(&evq_mutex);

        if (code >= 100) {
            key_event(code - 100, 0);   /* release */
        } else if (code > 0) {
            key_event(code - 1, 1);     /* press */
            wake = 1;
        }

        pthread_mutex_lock(&evq_mutex);
    }
    pthread_mutex_unlock(&evq_mutex);

    return wake;
}

/* ------------------------------------------------------------------
 * Stub X11 functions
 * ------------------------------------------------------------------ */

Pixmap XCreateBitmapFromData(Display *d, Window w, char *data, int width, int height)
{
    Pixmap p;
    p.data   = data;
    p.width  = width;
    p.height = height;
    return p;
}

void XClearArea(Display *d, Window w, int x, int y, int width, int height, int boo)
{
    (void)d; (void)w; (void)x; (void)y; (void)width; (void)height; (void)boo;
}

void XCopyPlane(Display *d, Pixmap map, Window w, GC gc,
                int a, int b, int x, int y, int width, int height, int boo)
{
    (void)d; (void)map; (void)w; (void)gc;
    (void)a; (void)b; (void)x; (void)y; (void)width; (void)height; (void)boo;
}

void XClearWindow(Display *d, Window w)
{
    (void)d; (void)w;
}

void refresh_display(void)
{
}

int exit_x48(int tell_x11)
{
    (void)tell_x11;
    return 0;
}

void refresh_icon(void)
{
}

void adjust_contrast(int contrast)
{
    (void)contrast;
}

void ShowConnections(char *wire, char *ir)
{
    (void)wire; (void)ir;
}
