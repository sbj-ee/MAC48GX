/*
 * lcd_mac.c - LCD rendering for macOS port of droid48/x48.
 * This is lcd.c from the JNI directory with JNI functions removed.
 * The disp_buf_short / disp_buf_header_short buffers are read directly
 * by the SDL2 render loop in main_sdl.c.
 */

#include "global.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include "hp48.h"
#include "hp48_emu.h"
#include "annunc.h"
#include "device.h"

static int last_annunc_state = -1;

display_t display;

#define DISP_ROWS               64
#define NIBS_PER_BUFFER_ROW     (NIBBLES_PER_ROW + 2)
#define DISP_ROWS_AS_SHORT      (DISP_ROWS * 2)
#define NIBS_PER_BUFFER_ROW_AS_SHORT 262
#define BLANK_COLOR             0x842d
#define ANDROID_BUF_LENGTH      (DISP_ROWS_AS_SHORT * NIBS_PER_BUFFER_ROW_AS_SHORT)
#define ANDROID_BUF_HEADER_LENGTH (14 * NIBS_PER_BUFFER_ROW_AS_SHORT)

unsigned char  disp_buf[DISP_ROWS][NIBS_PER_BUFFER_ROW];
unsigned char  lcd_buffer[DISP_ROWS][NIBS_PER_BUFFER_ROW];
unsigned short disp_buf_short[ANDROID_BUF_LENGTH];
unsigned short disp_buf_header_short[ANDROID_BUF_HEADER_LENGTH];
uint8_t        ann_boolean[6];

Pixmap nibble_maps[16];

unsigned char nibbles[16][2] = {
    { 0x00, 0x00 }, /* ---- */
    { 0x03, 0x03 }, /* *--- */
    { 0x0c, 0x0c }, /* -*-- */
    { 0x0f, 0x0f }, /* **-- */
    { 0x30, 0x30 }, /* --*- */
    { 0x33, 0x33 }, /* *-*- */
    { 0x3c, 0x3c }, /* -**- */
    { 0x3f, 0x3f }, /* ***- */
    { 0xc0, 0xc0 }, /* ---* */
    { 0xc3, 0xc3 }, /* *--* */
    { 0xcc, 0xcc }, /* -*-* */
    { 0xcf, 0xcf }, /* **-* */
    { 0xf0, 0xf0 }, /* --** */
    { 0xf3, 0xf3 }, /* *-** */
    { 0xfc, 0xfc }, /* -*** */
    { 0xff, 0xff }  /* **** */
};

unsigned short nibbles_short[16][8] = {
    { BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR },
    { 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR },
    { BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR },
    { 0x0000, 0x0000, 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR },
    { BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR },
    { 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR },
    { BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000, 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR },
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR },
    { BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000 },
    { 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000 },
    { BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000 },
    { 0x0000, 0x0000, 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000 },
    { BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000, 0x0000, 0x0000 },
    { 0x0000, 0x0000, BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000, 0x0000, 0x0000 },
    { BLANK_COLOR, BLANK_COLOR, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }
};

static unsigned char nibble_bits[16];

int flipable = 0;

static void fill_disp_buf_short(int x, int y, int v)
{
    int xx = x * 8;
    int yy = y * 2;
    if (xx >= NIBS_PER_BUFFER_ROW_AS_SHORT || yy >= DISP_ROWS_AS_SHORT)
        return;
    if (xx == NIBS_PER_BUFFER_ROW_AS_SHORT - 6) {
        memcpy(&disp_buf_short[NIBS_PER_BUFFER_ROW_AS_SHORT * yy + xx],
               &nibbles_short[v][0], 12);
        memcpy(&disp_buf_short[NIBS_PER_BUFFER_ROW_AS_SHORT * (yy + 1) + xx],
               &nibbles_short[v][0], 12);
    } else {
        memcpy(&disp_buf_short[NIBS_PER_BUFFER_ROW_AS_SHORT * yy + xx],
               &nibbles_short[v][0], 16);
        memcpy(&disp_buf_short[NIBS_PER_BUFFER_ROW_AS_SHORT * (yy + 1) + xx],
               &nibbles_short[v][0], 16);
    }
    flipable = 1;
}

void init_nibble_maps(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        nibble_maps[i] = XCreateBitmapFromData(dpy, disp.win,
                                               (char *)nibbles[i], 8, 2);
    }
}

void init_display(void)
{
    /* Initialize the header buffer with blank color */
    int i;
    for (i = 0; i < ANDROID_BUF_HEADER_LENGTH; i++)
        disp_buf_header_short[i] = BLANK_COLOR;

    disp.mapped = 1;
    display.on = (int)(saturn.disp_io & 0x8) >> 3;

    display.disp_start = (saturn.disp_addr & 0xffffe);
    display.offset     = (saturn.disp_io & 0x7);
    disp.offset        = 2 * display.offset;

    display.lines = (saturn.line_count & 0x3f);
    if (display.lines == 0)
        display.lines = 63;
    disp.lines = 2 * display.lines;
    if (disp.lines < 110)
        disp.lines = 110;

    if (display.offset > 3)
        display.nibs_per_line = (NIBBLES_PER_ROW + saturn.line_offset + 2) & 0xfff;
    else
        display.nibs_per_line = (NIBBLES_PER_ROW + saturn.line_offset) & 0xfff;

    display.disp_end  = display.disp_start +
                        (display.nibs_per_line * (display.lines + 1));
    display.menu_start = saturn.menu_addr;
    display.menu_end   = saturn.menu_addr + 0x110;
    display.contrast   = saturn.contrast_ctrl;
    display.contrast  |= ((saturn.disp_test & 0x1) << 4);
    display.annunc     = saturn.annunc;

    memset(disp_buf,   0xf0, sizeof(disp_buf));
    memset(lcd_buffer, 0xf0, sizeof(lcd_buffer));

    init_nibble_maps();
}

static inline void draw_nibble(int c, int r, int val)
{
    val &= 0x0f;
    if (val != lcd_buffer[r][c]) {
        lcd_buffer[r][c] = val;
        fill_disp_buf_short(c, r, val);
    }
}

static inline void draw_row(long addr, int row)
{
    int i, v;
    int line_length = NIBBLES_PER_ROW;
    if ((display.offset > 3) && (row <= display.lines))
        line_length += 2;
    for (i = 0; i < line_length; i++) {
        v = read_nibble(addr + i);
        if (v != disp_buf[row][i]) {
            disp_buf[row][i] = v;
            draw_nibble(i, row, v);
        }
    }
}

void update_display(void)
{
    int i, j;
    long addr;
    static int old_offset = -1;
    static int old_lines  = -1;

    if (display.on) {
        addr = display.disp_start;
        if (display.offset != old_offset) {
            memset(disp_buf,   0xf0, (size_t)((display.lines+1) * NIBS_PER_BUFFER_ROW));
            memset(lcd_buffer, 0xf0, (size_t)((display.lines+1) * NIBS_PER_BUFFER_ROW));
            old_offset = display.offset;
        }
        if (display.lines != old_lines) {
            memset(&disp_buf[56][0],   0xf0, (size_t)(8 * NIBS_PER_BUFFER_ROW));
            memset(&lcd_buffer[56][0], 0xf0, (size_t)(8 * NIBS_PER_BUFFER_ROW));
            old_lines = display.lines;
        }
        for (i = 0; i <= display.lines; i++) {
            draw_row(addr, i);
            addr += display.nibs_per_line;
        }
        if (i < DISP_ROWS) {
            addr = display.menu_start;
            for (; i < DISP_ROWS; i++) {
                draw_row(addr, i);
                addr += NIBBLES_PER_ROW;
            }
        }
    } else {
        memset(disp_buf, 0xf0, sizeof(disp_buf));
        for (i = 0; i < 64; i++)
            for (j = 0; j < NIBBLES_PER_ROW; j++)
                draw_nibble(j, i, 0x00);
    }
}

void redraw_display(void)
{
    XClearWindow(dpy, disp.win);
    memset(disp_buf,   0, sizeof(disp_buf));
    memset(lcd_buffer, 0, sizeof(lcd_buffer));
    update_display();
}

void disp_draw_nibble(word_20 addr, word_4 val)
{
    long offset;
    int  x, y;

    offset = (addr - display.disp_start);
    x = offset % display.nibs_per_line;
    if (x < 0 || x > 35)
        return;
    if (display.nibs_per_line != 0) {
        y = offset / display.nibs_per_line;
        if (y < 0 || y > 63)
            return;
        if (val != disp_buf[y][x]) {
            disp_buf[y][x] = val;
            draw_nibble(x, y, val);
        }
    } else {
        for (y = 0; y < display.lines; y++) {
            if (val != disp_buf[y][x]) {
                disp_buf[y][x] = val;
                draw_nibble(x, y, val);
            }
        }
    }
}

void menu_draw_nibble(word_20 addr, word_4 val)
{
    long offset;
    int  x, y;

    offset = (addr - display.menu_start);
    x = offset % NIBBLES_PER_ROW;
    y = display.lines + (offset / NIBBLES_PER_ROW) + 1;
    if (val != disp_buf[y][x]) {
        disp_buf[y][x] = val;
        draw_nibble(x, y, val);
    }
}

struct ann_struct {
    int            bit;
    int            x, y;
    unsigned int   width, height;
    unsigned char *bits;
    Pixmap         pixmap;
} ann_tbl[] = {
    { ANN_LEFT,    16,  4, ann_left_width,    ann_left_height,    ann_left_bits    },
    { ANN_RIGHT,   61,  4, ann_right_width,   ann_right_height,   ann_right_bits   },
    { ANN_ALPHA,  106,  4, ann_alpha_width,   ann_alpha_height,   ann_alpha_bits   },
    { ANN_BATTERY,151,  4, ann_battery_width, ann_battery_height, ann_battery_bits },
    { ANN_BUSY,   196,  4, ann_busy_width,    ann_busy_height,    ann_busy_bits    },
    { ANN_IO,     241,  4, ann_io_width,      ann_io_height,      ann_io_bits      },
    { 0 }
};

void draw_annunc(void)
{
    int val = display.annunc;
    int i;

    if (val == last_annunc_state)
        return;
    last_annunc_state = val;

    for (i = 0; ann_tbl[i].bit; i++)
        ann_boolean[i] = ((ann_tbl[i].bit & val) == ann_tbl[i].bit);

    flipable = 1;
}

void redraw_annunc(void)
{
    last_annunc_state = -1;
    draw_annunc();
}

void init_annunc(void)
{
    int i;
    for (i = 0; ann_tbl[i].bit; i++) {
        ann_tbl[i].pixmap = XCreateBitmapFromData(dpy, disp.win,
                                                   (char *)ann_tbl[i].bits,
                                                   ann_tbl[i].width,
                                                   ann_tbl[i].height);
    }
}
