/*
 * main_sdl.c - macOS/SDL2 main for MAC48GX/x48 emulator.
 *
 * Copyright (C) 2026  MAC48GX contributors
 * Based on x48 by Eddie C. Dost (Copyright (C) 1994-2005)
 * and droid48 by Arnaud Brochard (shagr4th)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v3.0 as published
 * by the Free Software Foundation.
 *
 * Original x48 emulator:
 *
 * Responsibilities:
 *  - Set up data file paths (~/.droid48/)
 *  - Set up SIGALRM timer (20 ms) for emulator timing
 *  - Provide blockConditionVariable() using pthread cond var
 *  - Run emulator in a background thread
 *  - SDL2 event loop: mouse input → button_pressed/released, render LCD
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
#include <math.h>
#include <pthread.h>
#include <pwd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "global.h"
#include "binio.h"
#include "hp48_emu.h"
#include "hp48.h"
#include "debugger.h"
#include "device.h"
#include "x48.h"
#include "rpl.h"

/* ------------------------------------------------------------------
 * Globals required by the emulator core
 * ------------------------------------------------------------------ */

char  *progname    = "MAC48GX";
char  *res_name    = "MAC48GX";
char  *res_class   = "Droid48";

int    saved_argc  = 0;
char **saved_argv  = NULL;

saturn_t saturn;
int      nb;
int      exit_state = 1;

/* Stub X11 globals */
Display *dpy    = NULL;
int      screen = 0;
disp_t   disp;
color_t *colors = NULL;

/* ------------------------------------------------------------------
 * Timing: SIGALRM drives got_alarm
 * ------------------------------------------------------------------ */

pthread_cond_t  uiConditionVariable = PTHREAD_COND_INITIALIZER;
pthread_mutex_t uiConditionMutex    = PTHREAD_MUTEX_INITIALIZER;

extern int got_alarm;   /* defined in actions.c */

static void sigalrm_handler(int sig)
{
    (void)sig;
    got_alarm = 1;
    /* Do NOT call pthread_mutex_lock here — it's not async-signal-safe.
     * blockConditionVariable uses timedwait so it wakes periodically. */
}

void blockConditionVariable(void)
{
    struct timeval  now;
    struct timespec ts;

    gettimeofday(&now, NULL);
    ts.tv_sec  = now.tv_sec;
    ts.tv_nsec = now.tv_usec * 1000 + 20000000;  /* +20 ms */
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_timedwait(&uiConditionVariable, &uiConditionMutex, &ts);
    pthread_mutex_unlock(&uiConditionMutex);
}

/* ------------------------------------------------------------------
 * File paths
 * ------------------------------------------------------------------ */

char files_path   [256];
char rom_filename [256];
char ram_filename [256];
char conf_filename[256];
char port1_filename[256];
char port2_filename[256];

static void setup_paths(void)
{
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }

    /* files_path must end with '/' — emulator concatenates filenames directly */
    snprintf(files_path, 256, "%s/.droid48/", home);

    /* These are just the file base names (emulator prepends files_path) */
    strncpy(rom_filename,   "rom",   255);
    strncpy(ram_filename,   "ram",   255);
    strncpy(conf_filename,  "hp48",  255);
    strncpy(port1_filename, "port1", 255);
    strncpy(port2_filename, "port2", 255);

    /* Create directory if needed */
    {
        char dir[256];
        strncpy(dir, files_path, 255);
        dir[255] = '\0';
        size_t len = strlen(dir);
        if (len > 1 && dir[len-1] == '/') dir[len-1] = '\0';
        mkdir(dir, 0755);
    }
}

static int copy_asset(const char *src, const char *dst)
{
    FILE *in  = fopen(src, "rb");
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

/*
 * Ensure ROM/RAM files are in ~/.droid48/.
 * We look for them next to the binary first (for development),
 * then in the app Resources dir, then fall back to the jni/ originals.
 */
static void ensure_data_files(const char *exe_dir)
{
    struct stat st;
    char full_rom[256], full_ram[256], full_conf[256];

    /* Build full paths */
    snprintf(full_rom,  256, "%s%s", files_path, rom_filename);
    snprintf(full_ram,  256, "%s%s", files_path, ram_filename);
    snprintf(full_conf, 256, "%s%s", files_path, conf_filename);

    /* ROM: copy from exe_dir, app bundle Resources, or jni dir */
    if (stat(full_rom, &st) != 0 || st.st_size < 1024) {
        char src[256];
        int found = 0;
        /* Try next to executable */
        snprintf(src, 256, "%s/rom", exe_dir);
        if (!found && stat(src, &st) == 0 && st.st_size > 1024) found = 1;
        /* Try app bundle Resources (exe_dir/../Resources/) */
        if (!found) {
            snprintf(src, 256, "%s/../Resources/rom", exe_dir);
            if (stat(src, &st) == 0 && st.st_size > 1024) found = 1;
        }
        if (found) {
            copy_asset(src, full_rom);
        } else {
            fprintf(stderr, "MAC48GX: cannot find ROM file.\n"
                    "Please copy a HP-48 ROM image to %s\n", full_rom);
        }
    }

    /* RAM: copy from exe_dir if not in data dir */
    if (stat(full_ram, &st) != 0) {
        char src[256];
        snprintf(src, 256, "%s/ram", exe_dir);
        if (stat(src, &st) == 0) {
            copy_asset(src, full_ram);
        } else {
            /* Create empty RAM */
            FILE *f = fopen(full_ram, "wb");
            if (f) {
                char zeros[131072];
                memset(zeros, 0, sizeof(zeros));
                fwrite(zeros, 1, sizeof(zeros), f);
                fclose(f);
            }
        }
    }

    /* Config: copy from exe_dir if not in data dir */
    if (stat(full_conf, &st) != 0) {
        char src[256];
        snprintf(src, 256, "%s/hp48", exe_dir);
        if (stat(src, &st) == 0)
            copy_asset(src, full_conf);
    }
}

/* ------------------------------------------------------------------
 * Emulator thread
 * ------------------------------------------------------------------ */

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
 * SDL2 display
 *
 * The LCD buffer is two arrays of RGB565 shorts:
 *   disp_buf_header_short[14 * 262]  — annunciator area (14 px tall)
 *   disp_buf_short       [128 * 262] — LCD area (128 px tall)
 *
 * We combine them into a single 262×142 SDL texture and scale up.
 *
 * Layout (all coordinates in logical screen pixels):
 *
 *   Unified scale: 3/2 = 1.5x
 *   Button orig coords: x 0..295 wide, y 0..425 tall
 *   Button screen:      444 wide × 639 tall
 *   LCD screen (1.5x):  393 wide × 213 tall, centered in 444
 *   Window:             444 wide × 860 tall
 *                       (fits on any Mac screen ≥ 900 logical px)
 * ------------------------------------------------------------------ */

#define LCD_W   262
#define LCD_H   128
#define HDR_H    14
#define FULL_H  (HDR_H + LCD_H)   /* 142 */

/* Scale: 2x horizontal, 10/7≈1.43x vertical (compact rows) */
#define SC_NUM   2
#define SC_DEN   1
#define SCV_NUM  13
#define SCV_DEN  10

/* LCD area in screen pixels (uses horizontal scale) */
#define LCD_SW  ((LCD_W * SC_NUM) / SC_DEN)    /* 458 */
#define LCD_SH  ((FULL_H * SC_NUM) / SC_DEN)   /* 248 */

/* Button area in screen pixels */
#define BTN_ORIG_W  296
#define BTN_ORIG_H  426
#define BTN_SW      ((BTN_ORIG_W * SC_NUM) / SC_DEN)    /* 518 */
#define BTN_SH      ((BTN_ORIG_H * SCV_NUM) / SCV_DEN)  /* 608 */

/* Window dimensions */
#define BTN_MARGIN 20
#define WIN_W   (BTN_SW + 2 * BTN_MARGIN)     /* 474 */
#define LCD_X   ((WIN_W - LCD_SW) / 2)        /* center LCD */
#define TITLE_H 22                            /* space for title above LCD */
#define LCD_Y   TITLE_H
#define BTN_X   BTN_MARGIN
#define BTN_Y   (LCD_Y + LCD_SH + 16)         /* space between LCD and buttons */
#define WIN_H   (BTN_Y + BTN_SH + 26)         /* space for CANCEL label */

extern unsigned short disp_buf_short[];        /* defined in lcd_mac.c */
extern unsigned short disp_buf_header_short[]; /* defined in lcd_mac.c */
extern int            flipable;                /* defined in lcd_mac.c */

/* Combined pixel buffer: HDR_H + LCD_H rows, LCD_W cols, RGB565 */
static Uint16 combined_buf[FULL_H * LCD_W];

/* Annunciator names drawn over the header strip */
static const char *ann_names[] = { "<<", ">>", "ALPHA", "BAT", "BUSY", "IO" };
/* Annunciator x positions (original coords) */
static const int ann_x[] = { 16, 61, 106, 151, 196, 241 };

extern uint8_t ann_boolean[6]; /* defined in lcd_mac.c */

extern int button_pressed(int b);
extern int button_released(int b);
extern void sdl_push_event(unsigned int code);  /* defined in x48_sdl.c */

/* Full button_t struct — must match the definition in x48_sdl.c exactly
 * so that array indexing works correctly. */
typedef struct {
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

extern button_t buttons[];

#define NUM_BUTTONS 49

/* ------------------------------------------------------------------
 * Audio — SDL2 audio callback generates square wave from speaker_counter
 * ------------------------------------------------------------------ */

#define AUDIO_RATE    44100
#define AUDIO_SAMPLES 1024

static SDL_AudioDeviceID audio_dev = 0;
static volatile int audio_delta = 0;   /* samples per half-cycle (0 = silent) */
static int audio_phase = 0;
static char audio_speaker_state = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    Sint16 *buf = (Sint16 *)stream;
    int samples = len / (int)sizeof(Sint16);
    int i;
    int delta = audio_delta;  /* snapshot — written by main thread */
    (void)userdata;

    if (delta <= 0) {
        memset(stream, 0, len);
        return;
    }

    for (i = 0; i < samples; i++) {
        audio_phase--;
        if (audio_phase <= 0) {
            audio_phase = delta;
            audio_speaker_state = !audio_speaker_state;
        }
        buf[i] = audio_speaker_state ? 8000 : -8000;
    }
}

/* Called from the main loop ~60 times/sec.
 * Reads device.speaker_counter and updates audio_delta. */
static void update_audio(void)
{
    static int accum_frames = 0;
    static int accum_count  = 0;

    accum_count += device.speaker_counter;
    device.speaker_counter = 0;
    accum_frames++;

    /* Accumulate over ~6 frames (~100ms at 60fps) for stable frequency */
    if (accum_frames >= 6) {
        if (accum_count > 0) {
            /* speaker_counter counts toggles in the period.
             * freq = toggles / (accum_frames * 16.67ms) / 2 (two toggles per cycle)
             * But actually each toggle is a half-cycle, so:
             * freq = toggles / (accum_frames / 60.0) / 2 */
            double dt = accum_frames / 60.0;
            double freq = accum_count / dt / 2.0;
            if (freq > 20.0 && freq < 20000.0) {
                audio_delta = (int)(AUDIO_RATE / (freq * 2.0));
            } else {
                audio_delta = 0;
            }
        } else {
            audio_delta = 0;
        }
        accum_frames = 0;
        accum_count  = 0;
    }
}

/* Convert original button coordinate to screen coordinate */
static int btn_to_screen_x(int bx)
{
    return BTN_X + (bx * SC_NUM) / SC_DEN;
}
static int btn_to_screen_y(int by)
{
    return BTN_Y + (by * SCV_NUM) / SCV_DEN;
}
static int btn_screen_w(unsigned int bw)
{
    return (bw * SC_NUM) / SC_DEN;
}
static int btn_screen_h(unsigned int bh)
{
    return (bh * SCV_NUM) / SCV_DEN;
}

/* Hit test: returns button index or -1 */
static int hit_test(int mx, int my)
{
    int i;
    for (i = 0; i < NUM_BUTTONS; i++) {
        int sx = btn_to_screen_x(buttons[i].x);
        int sy = btn_to_screen_y(buttons[i].y);
        int sw = btn_screen_w(buttons[i].w);
        int sh = btn_screen_h(buttons[i].h);
        if (mx >= sx && mx < sx + sw && my >= sy && my < sy + sh)
            return i;
    }
    return -1;
}

/* Map SDL keysym to button index (-1 if none) */
static int keysym_to_button(SDL_Keycode sym)
{
    switch (sym) {
    /* Number row */
    case SDLK_0:          return 45; /* BUTTON_0 */
    case SDLK_1:          return 40;
    case SDLK_2:          return 41;
    case SDLK_3:          return 42;
    case SDLK_4:          return 35;
    case SDLK_5:          return 36;
    case SDLK_6:          return 37;
    case SDLK_7:          return 30;
    case SDLK_8:          return 31;
    case SDLK_9:          return 32;
    case SDLK_PERIOD:     return 46; /* PERIOD */
    case SDLK_SPACE:      return 47; /* SPC */
    /* Arithmetic */
    case SDLK_PLUS:
    case SDLK_KP_PLUS:    return 48; /* PLUS */
    case SDLK_MINUS:
    case SDLK_KP_MINUS:   return 43; /* MINUS */
    case SDLK_ASTERISK:
    case SDLK_KP_MULTIPLY:return 38; /* MUL */
    case SDLK_SLASH:
    case SDLK_KP_DIVIDE:  return 33; /* DIV */
    /* Enter / Delete */
    case SDLK_RETURN:
    case SDLK_KP_ENTER:   return 24; /* ENTER */
    case SDLK_BACKSPACE:  return 28; /* BS */
    case SDLK_DELETE:     return 27; /* DEL */
    /* Arrow keys */
    case SDLK_LEFT:       return 15; /* BUTTON_LEFT */
    case SDLK_RIGHT:      return 17; /* BUTTON_RIGHT */
    case SDLK_UP:         return 10; /* BUTTON_UP */
    case SDLK_DOWN:       return 16; /* BUTTON_DOWN */
    /* Escape → ON */
    case SDLK_ESCAPE:     return 44; /* BUTTON_ON */
    /* Function keys → menu row A-F */
    case SDLK_F1:         return 0;
    case SDLK_F2:         return 1;
    case SDLK_F3:         return 2;
    case SDLK_F4:         return 3;
    case SDLK_F5:         return 4;
    case SDLK_F6:         return 5;
    /* Trig */
    case SDLK_s:          return 18; /* SIN */
    case SDLK_c:          return 19; /* COS */
    case SDLK_t:          return 20; /* TAN */
    /* Misc */
    case SDLK_e:          return 26; /* EEX */
    case SDLK_n:          return 25; /* NEG (change sign) */
    case SDLK_a:          return 29; /* ALPHA */
    default:              return -1;
    }
}

/* ------------------------------------------------------------------
 * Fonts
 * ------------------------------------------------------------------ */
static TTF_Font *font_btn      = NULL;  /* button face label (13pt)      */
static TTF_Font *font_btn_lg   = NULL;  /* large number labels (16pt)    */
static TTF_Font *font_btn_sm   = NULL;  /* small labels on menu/ENTER    */
static TTF_Font *font_shift    = NULL;  /* shift/alpha labels (9pt)      */
static TTF_Font *font_title    = NULL;  /* title text (11pt bold)        */

/* ------------------------------------------------------------------
 * HP-48GX color palette
 * ------------------------------------------------------------------ */
#define CLR_BODY_R   62
#define CLR_BODY_G   78
#define CLR_BODY_B   78

static const SDL_Color clr_white   = { 220, 220, 220, 255 };
static const SDL_Color clr_lshift  = { 190, 140, 230, 255 };  /* purple — left shift (◄) */
static const SDL_Color clr_rshift  = { 130, 210, 190, 255 };  /* green/teal — right shift (►) */
static const SDL_Color clr_alpha   = { 150, 220, 200, 255 };  /* brighter green for alpha letters */
static const SDL_Color clr_title   = { 200, 200, 200, 255 };

/* Primary button face labels — using ASCII where Unicode fails */
static const char *btn_labels[NUM_BUTTONS] = {
    "",     "",     "",     "",     "",     "",          /*  0- 5: menu (drawn as boxes) */
    "MTH",  "PRG",  "CST",  "VAR",  "",    "NXT",      /*  6-11: UP=arrow drawn manually */
    "'",    "STO",  "EVAL", "",     "",     "",         /* 12-17: arrows drawn manually */
    "SIN",  "COS",  "TAN",  "",     "",    "1/x",     /* 18-23: sqrt, y^x drawn manually */
    "ENTER","+/-",  "EEX",  "DEL",  "",                /* 24-28: BS=arrow drawn manually */
    "",     "7",    "8",    "9",    "\xc3\xb7",         /* 29-33: alpha drawn manually, ÷ */
    "",     "4",    "5",    "6",    "\xc3\x97",         /* 34-38: SHL drawn manually, × */
    "",     "1",    "2",    "3",    "-",                /* 39-43: SHR drawn manually */
    "ON",   "0",    ".",    "SPC",  "+"                 /* 44-48 */
};

/* Translate HP-48 special character codes to displayable text.
 * Returns pointer to static buffer or the original string. */
static const char *translate_hp48_label(const char *s)
{
    static char tbuf[32];
    if (!s || !s[0]) return NULL;

    unsigned char c = (unsigned char)s[0];

    /* Control character codes used on the bottom rows */
    switch (c) {
    case 0x01: return "\xe2\x86\xb5";     /* ↵ return arrow (above .) */
    case 0x02: return ",";                 /* comma (above .) */
    case 0x03: return "\xe2\x86\x92";     /* → right arrow (above 0) */
    case 0x04: return "=";                 /* equals (above 0) */
    case 0x05: return "\xcf\x80";          /* π pi (above SPC) */
    }

    /* Single-letter codes representing HP-48 special chars on operator keys */
    if (s[1] == '\0' || (s[1] == ' ' && s[2] == '\0')) {
        switch (c) {
        case 'b': return "\xe2\x88\x82";   /* ∂ partial derivative */
        case 'c': return "\xe2\x88\xab";   /* ∫ integral */
        case 'd': return "\xce\xa3";       /* Σ sigma */
        case 'n': return "x\xc2\xb2";     /* x² */
        case 'o': return "\xe2\x81\xbf\xe2\x88\x9ay"; /* ⁿ√y */
        case 'p': return "10\xe2\x81\xbf"; /* 10ⁿ */
        case 'q': return "e\xe2\x81\xbf";  /* eⁿ */
        case 'r': return "( )";
        case 's': return "#";
        case 't': return "[ ]";
        case 'u': return "_";
        case 'v': return "\xc2\xab \xc2\xbb";  /* « » */
        case 'w': return "\" \"";
        case 'x': return "{ }";
        case 'y': return ": :";
        case 'z': return "\xe2\x88\xa0";   /* ∠ angle (above SPC) */
        }
    }

    /* Already printable */
    if (c >= 0x20) return s;

    return NULL;  /* unprintable, skip */
}

/* Check if a label has displayable content after translation */
static int is_printable_label(const char *s)
{
    return translate_hp48_label(s) != NULL;
}

/* Helper: copy label trimming trailing spaces */
static const char *trim_label(const char *s, char *buf, int bufsz)
{
    int len;
    if (!s || !s[0]) return s;
    len = (int)strlen(s);
    if (len >= bufsz) len = bufsz - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    while (len > 0 && buf[len-1] == ' ') buf[--len] = '\0';
    return buf;
}

/* ------------------------------------------------------------------
 * Text drawing helpers
 * ------------------------------------------------------------------ */

/* Draw a filled rounded rectangle using scanlines */
static void fill_rounded_rect(SDL_Renderer *renderer, int x, int y, int w, int h, int rad)
{
    int row;
    if (rad > h/2) rad = h/2;
    if (rad > w/2) rad = w/2;

    for (row = 0; row < h; row++) {
        int x0 = x, x1 = x + w - 1;
        if (row < rad) {
            /* Top rounded edge */
            int dy = rad - row;
            int dx = rad - (int)(sqrt((double)(rad*rad - dy*dy)) + 0.5);
            x0 += dx;
            x1 -= dx;
        } else if (row >= h - rad) {
            /* Bottom rounded edge */
            int dy = row - (h - 1 - rad);
            int dx = rad - (int)(sqrt((double)(rad*rad - dy*dy)) + 0.5);
            x0 += dx;
            x1 -= dx;
        }
        SDL_RenderDrawLine(renderer, x0, y + row, x1, y + row);
    }
}

/* Draw text centered in a rectangle */
static void draw_text_centered(SDL_Renderer *renderer, TTF_Font *font,
                                const char *text, SDL_Color color,
                                int cx, int cy, int max_w)
{
    SDL_Surface *surf;
    SDL_Texture *tex;
    int tw, th;

    if (!font || !text || !text[0]) return;
    surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }
    tw = surf->w;  th = surf->h;
    SDL_FreeSurface(surf);
    if (max_w > 0 && tw > max_w) { th = th * max_w / tw; tw = max_w; }
    SDL_Rect dst = { cx - tw/2, cy - th/2, tw, th };
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

/* Draw text left-aligned at (x, cy) */
static void draw_text_left(SDL_Renderer *renderer, TTF_Font *font,
                            const char *text, SDL_Color color,
                            int x, int cy, int max_w)
{
    SDL_Surface *surf;
    SDL_Texture *tex;
    int tw, th;

    if (!font || !text || !text[0]) return;
    surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }
    tw = surf->w;  th = surf->h;
    SDL_FreeSurface(surf);
    if (max_w > 0 && tw > max_w) { th = th * max_w / tw; tw = max_w; }
    SDL_Rect dst = { x, cy - th/2, tw, th };
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

/* Draw text right-aligned ending at (x, cy) */
static void draw_text_right(SDL_Renderer *renderer, TTF_Font *font,
                             const char *text, SDL_Color color,
                             int x, int cy, int max_w)
{
    SDL_Surface *surf;
    SDL_Texture *tex;
    int tw, th;

    if (!font || !text || !text[0]) return;
    surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }
    tw = surf->w;  th = surf->h;
    SDL_FreeSurface(surf);
    if (max_w > 0 && tw > max_w) { th = th * max_w / tw; tw = max_w; }
    SDL_Rect dst = { x - tw, cy - th/2, tw, th };
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

/* ------------------------------------------------------------------
 * Render — full HP-48GX calculator face
 * ------------------------------------------------------------------ */
static void render(SDL_Renderer *renderer, SDL_Texture *lcd_tex)
{
    int i;

    /* --- Background: calculator body with subtle border --- */
    SDL_SetRenderDrawColor(renderer, CLR_BODY_R - 8, CLR_BODY_G - 8, CLR_BODY_B - 8, 255);
    SDL_RenderClear(renderer);
    {
        /* Slightly lighter inner area */
        SDL_Rect inner = { 4, 4, WIN_W - 8, WIN_H - 8 };
        SDL_SetRenderDrawColor(renderer, CLR_BODY_R, CLR_BODY_G, CLR_BODY_B, 255);
        SDL_RenderFillRect(renderer, &inner);
    }

    /* --- LCD bezel (layered frame for depth) --- */
    {
        /* Outer bezel — dark */
        int bx = LCD_X - 6, by = LCD_Y - 6;
        int bw = LCD_SW + 12, bh = LCD_SH + 12;
        SDL_SetRenderDrawColor(renderer, 35, 40, 40, 255);
        fill_rounded_rect(renderer, bx, by, bw, bh, 6);
        /* Inner bezel — slightly lighter for depth */
        SDL_SetRenderDrawColor(renderer, 48, 55, 55, 255);
        fill_rounded_rect(renderer, bx+2, by+2, bw-4, bh-4, 4);
    }

    /* --- Update LCD texture only when display changed --- */
    if (flipable) {
        memcpy(combined_buf,
               disp_buf_header_short,
               HDR_H * LCD_W * sizeof(Uint16));
        memcpy(combined_buf + HDR_H * LCD_W,
               disp_buf_short,
               LCD_H * LCD_W * sizeof(Uint16));
        SDL_UpdateTexture(lcd_tex, NULL, combined_buf, LCD_W * sizeof(Uint16));
        flipable = 0;
    }

    /* Scale and blit LCD */
    {
        SDL_Rect dst = { LCD_X, LCD_Y, LCD_SW, LCD_SH };
        SDL_RenderCopy(renderer, lcd_tex, NULL, &dst);
    }

    /* --- Draw buttons --- */
    for (i = 0; i < NUM_BUTTONS; i++) {
        int sx = btn_to_screen_x(buttons[i].x);
        int sy = btn_to_screen_y(buttons[i].y);
        int sw = btn_screen_w(buttons[i].w);
        int sh = btn_screen_h(buttons[i].h);
        const char *lbl = btn_labels[i];
        int pressed = buttons[i].pressed;

        /* Pressed buttons shift down 2px for physical feel */
        if (pressed) sy += 2;

        /* ---- Button face color ---- */
        if (pressed) {
            SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
        } else if (i < 6) {
            /* Menu keys A-F: light gray */
            SDL_SetRenderDrawColor(renderer, 160, 165, 165, 255);
        } else if (i == 44) {
            /* ON button: slightly distinct dark green-gray */
            SDL_SetRenderDrawColor(renderer, 28, 38, 32, 255);
        } else {
            /* All other keys: dark/black */
            SDL_SetRenderDrawColor(renderer, 32, 32, 36, 255);
        }
        {
            int rad = (i < 6) ? 4 : 5;
            fill_rounded_rect(renderer, sx, sy, sw, sh, rad);
            /* Subtle highlight line on top edge for 3D look */
            if (!buttons[i].pressed && i >= 6) {
                SDL_SetRenderDrawColor(renderer, 55, 55, 60, 255);
                SDL_RenderDrawLine(renderer, sx + rad, sy + 1, sx + sw - rad, sy + 1);
            }
        }

        /* ---- Button face label ---- */
        if (i < 6) {
            /* Menu keys: solid light gray, no additional decoration */
        } else if (i == 10) {
            /* UP arrow triangle */
            int cx = sx + sw/2, cy = sy + sh/2;
            SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
            { int y; for (y = -5; y <= 4; y++) {
                int hw = (y + 5) * 5 / 9;
                SDL_RenderDrawLine(renderer, cx-hw, cy+y, cx+hw, cy+y);
            }}
        } else if (i == 15) {
            /* LEFT arrow triangle */
            int cx = sx + sw/2, cy = sy + sh/2;
            SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
            { int x; for (x = -5; x <= 4; x++) {
                int hh = (x + 5) * 5 / 9;
                SDL_RenderDrawLine(renderer, cx+x, cy-hh, cx+x, cy+hh);
            }}
        } else if (i == 16) {
            /* DOWN arrow triangle */
            int cx = sx + sw/2, cy = sy + sh/2;
            SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
            { int y; for (y = -4; y <= 5; y++) {
                int hw = (5 - y) * 5 / 9;
                SDL_RenderDrawLine(renderer, cx-hw, cy+y, cx+hw, cy+y);
            }}
        } else if (i == 17) {
            /* RIGHT arrow triangle */
            int cx = sx + sw/2, cy = sy + sh/2;
            SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
            { int x; for (x = -4; x <= 5; x++) {
                int hh = (5 - x) * 5 / 9;
                SDL_RenderDrawLine(renderer, cx+x, cy-hh, cx+x, cy+hh);
            }}
        } else if (i == 21) {
            /* SQRT symbol */
            int cx = sx + sw/2, cy = sy + sh/2;
            SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
            SDL_RenderDrawLine(renderer, sx+6, cy-2, sx+10, cy+6);
            SDL_RenderDrawLine(renderer, sx+10, cy+6, sx+16, cy-6);
            SDL_RenderDrawLine(renderer, sx+16, cy-6, sx+sw-6, cy-6);
            (void)cx;
        } else if (i == 28) {
            /* Backspace arrow ← */
            int cx = sx + sw/2, cy = sy + sh/2;
            SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
            SDL_RenderDrawLine(renderer, cx-8, cy, cx+8, cy);
            SDL_RenderDrawLine(renderer, cx-8, cy, cx-3, cy-4);
            SDL_RenderDrawLine(renderer, cx-8, cy, cx-3, cy+4);
        } else if (i == 22) {
            /* y^x with superscript x */
            draw_text_centered(renderer, font_btn_lg, "y", clr_white,
                               sx + sw/2 - 8, sy + sh/2 + 2, 0);
            draw_text_centered(renderer, font_btn, "x", clr_white,
                               sx + sw/2 + 10, sy + sh/2 - 8, 0);
        } else if (i == 29) {
            /* Alpha symbol α (Greek lowercase alpha, UTF-8: 0xCE 0xB1) */
            draw_text_centered(renderer, font_btn_lg, "\xce\xb1", clr_white,
                               sx + sw/2, sy + sh/2, sw - 4);
        } else if (i == 34) {
            /* Left shift: bold ← arrow in purple */
            int cx = sx + sw/2, cy = sy + sh/2;
            int r;
            SDL_SetRenderDrawColor(renderer, clr_lshift.r, clr_lshift.g, clr_lshift.b, 255);
            /* Thick shaft */
            for (r = -2; r <= 2; r++)
                SDL_RenderDrawLine(renderer, cx-10, cy+r, cx+12, cy+r);
            /* Large arrowhead */
            { int dy; for (dy = -7; dy <= 7; dy++) {
                int tip = (7 - (dy < 0 ? -dy : dy)) * 10 / 7;
                SDL_RenderDrawLine(renderer, cx-10-tip, cy+dy, cx-10, cy+dy);
            }}
        } else if (i == 39) {
            /* Right shift: bold → arrow in green */
            int cx = sx + sw/2, cy = sy + sh/2;
            int r;
            SDL_SetRenderDrawColor(renderer, clr_rshift.r, clr_rshift.g, clr_rshift.b, 255);
            /* Thick shaft */
            for (r = -2; r <= 2; r++)
                SDL_RenderDrawLine(renderer, cx-12, cy+r, cx+10, cy+r);
            /* Large arrowhead */
            { int dy; for (dy = -7; dy <= 7; dy++) {
                int tip = (7 - (dy < 0 ? -dy : dy)) * 10 / 7;
                SDL_RenderDrawLine(renderer, cx+10, cy+dy, cx+10+tip, cy+dy);
            }}
        } else if (lbl[0]) {
            /* Text label on button face */
            SDL_Color fc = clr_white;
            TTF_Font *f = font_btn;
            /* Number keys + operators: use large font */
            if (buttons[i].font_size == 1 && font_btn_lg) f = font_btn_lg;
            if (i == 33 || i == 38 || i == 43 || i == 48) f = font_btn_lg; /* ÷ × - + */
            if (i == 24 && font_btn_sm) f = font_btn_sm; /* ENTER */
            draw_text_centered(renderer, f, lbl, fc,
                               sx + sw/2, sy + sh/2, sw - 4);
        }

        /* Alpha letters (A-Z) omitted — not needed with a laptop keyboard */

        /* ---- Left / Right shift labels above button ---- */
        {
            int has_left  = is_printable_label(buttons[i].left);
            int has_right = is_printable_label(buttons[i].right);
            int ly = sy - 11;  /* above the button */
            char lbuf[32], rbuf[32];
            const char *ltxt = has_left  ? trim_label(translate_hp48_label(buttons[i].left),  lbuf, sizeof(lbuf)) : NULL;
            const char *rtxt = has_right ? trim_label(translate_hp48_label(buttons[i].right), rbuf, sizeof(rbuf)) : NULL;
            /* Max label width: half the button width to avoid overlap */
            if (has_left && has_right) {
                /* Spread labels across full column width (button + gap).
                 * Clip each label to its half to prevent overlap. */
                int col = (50 * SC_NUM) / SC_DEN;
                if (sw > col) col = sw;
                int half = col / 2 - 2;
                draw_text_centered(renderer, font_shift, ltxt,
                                   clr_lshift, sx + col/4, ly, half);
                draw_text_centered(renderer, font_shift, rtxt,
                                   clr_rshift, sx + col*3/4, ly, half);
            } else if (has_left) {
                draw_text_centered(renderer, font_shift, ltxt,
                                   clr_lshift, sx + sw/2, ly, sw + 10);
            } else if (has_right) {
                draw_text_centered(renderer, font_shift, rtxt,
                                   clr_rshift, sx + sw/2, ly, sw + 10);
            }
        }

        /* ---- Sub label (below button, e.g. "CANCEL" under ON) ---- */
        if (buttons[i].sub && buttons[i].sub[0]) {
            draw_text_centered(renderer, font_shift, buttons[i].sub, clr_white,
                               sx + sw/2, sy + sh + 10, sw + 15);
        }
    }

    /* --- Title above LCD --- */
    {
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif
        char title_buf[64];
        snprintf(title_buf, sizeof(title_buf), "MAC48GX %s", APP_VERSION);
        draw_text_left(renderer, font_title, title_buf, clr_title,
                       LCD_X, TITLE_H / 2, 0);
    }

    SDL_RenderPresent(renderer);
}

/* ------------------------------------------------------------------
 * Test mode: --test flag runs tests in a background thread while
 * the calculator GUI remains visible and responsive.
 * ------------------------------------------------------------------ */

#include "test_cases.h"

static volatile int test_finished = 0;
static int test_exit_code = 0;

static void *test_thread_func(void *arg)
{
    (void)arg;
    /* Wait for emulator to boot */
    usleep(1500000);
    printf("Running tests (visible in calculator window)...\n");

    run_all_tests();

    test_exit_code = (tests_failed > 0) ? 1 : 0;
    test_finished = 1;
    return NULL;
}

/* ------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    SDL_Window   *window   = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture  *lcd_tex  = NULL;
    pthread_t     emu_thread;
    struct sigaction sa;
    struct itimerval it;
    int pressed_btn = -1;
    int test_mode = 0;
    char exe_dir[256];

    /* Check for --test flag */
    {
        int i;
        for (i = 1; i < argc; i++)
            if (strcmp(argv[i], "--test") == 0) test_mode = 1;
    }

    /* Determine directory of executable for finding assets */
    {
        char *last_slash;
        strncpy(exe_dir, argv[0], 256-1);
        last_slash = strrchr(exe_dir, '/');
        if (last_slash)
            *last_slash = '\0';
        else
            strcpy(exe_dir, ".");
    }

    /* File paths */
    setup_paths();
    ensure_data_files(exe_dir);

    /* SIGALRM for emulator timing */
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigalrm_handler;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    it.it_interval.tv_sec  = 0;
    it.it_interval.tv_usec = 20000;  /* 20 ms → 50 Hz */
    it.it_value.tv_sec     = 0;
    it.it_value.tv_usec    = 20000;
    setitimer(ITIMER_REAL, &it, NULL);

    /* SDL2 init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    /* Audio init */
    {
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof(want));
        want.freq     = AUDIO_RATE;
        want.format   = AUDIO_S16SYS;
        want.channels = 1;
        want.samples  = AUDIO_SAMPLES;
        want.callback = audio_callback;
        audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audio_dev > 0) {
            SDL_PauseAudioDevice(audio_dev, 0);  /* start playing */
        } else {
            fprintf(stderr, "SDL audio: %s (sound disabled)\n", SDL_GetError());
        }
    }

    /* Font init */
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return 1;
    }
    {
        const char *fn = "/System/Library/Fonts/Helvetica.ttc";
        const char *fb = "/System/Library/Fonts/HelveticaNeue.ttc";
        /* Asana-Math from droid48 assets — has all HP-48 math symbols */
        const char *fm = "../app/src/main/assets/Asana-Math.ttf";
        char fm_bundle[256];
        snprintf(fm_bundle, sizeof(fm_bundle), "%s/../Resources/Asana-Math.ttf", exe_dir);

        font_btn    = TTF_OpenFont(fn, 22);
        font_btn_lg = TTF_OpenFont(fn, 30);
        font_btn_sm = TTF_OpenFont(fn, 17);
        font_title  = TTF_OpenFont(fb, 14);
        /* Try Asana-Math for shift labels: bundle → local → assets */
        /* Asana-Math: has all HP-48 math symbols (∂ ∫ Σ π ∠ → ↵ ²) */
        font_shift  = TTF_OpenFont(fm_bundle, 14);
        if (!font_shift) font_shift = TTF_OpenFont("Asana-Math.ttf", 14);
        if (!font_shift) font_shift = TTF_OpenFont(fm, 14);
        if (!font_shift) font_shift = TTF_OpenFont(fb, 14);
        if (!font_btn) {
            fn = "/System/Library/Fonts/Monaco.ttf";
            font_btn    = TTF_OpenFont(fn, 12);
            font_btn_lg = TTF_OpenFont(fn, 16);
            font_btn_sm = TTF_OpenFont(fn, 10);
            if (!font_shift) font_shift = TTF_OpenFont(fn, 8);
            font_title  = TTF_OpenFont(fn, 11);
        }
        if (!font_btn)
            fprintf(stderr, "Warning: could not load fonts\n");
        if (font_btn)    TTF_SetFontStyle(font_btn, TTF_STYLE_BOLD);
        if (font_btn_lg) TTF_SetFontStyle(font_btn_lg, TTF_STYLE_BOLD);
        if (font_btn_sm) TTF_SetFontStyle(font_btn_sm, TTF_STYLE_BOLD);
        if (font_title)  TTF_SetFontStyle(font_title, TTF_STYLE_BOLD);
    }

    {
        char win_title[64];
        snprintf(win_title, sizeof(win_title), "MAC48GX %s", APP_VERSION);
        window = SDL_CreateWindow(win_title,
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WIN_W, WIN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) {
            fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
            return 1;
        }
    }

    /* Map logical coordinates to the full window (fixes HiDPI/Retina) */
    SDL_RenderSetLogicalSize(renderer, WIN_W, WIN_H);

    /* LCD texture — RGB565, 262 wide × 142 tall */
    lcd_tex = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STREAMING,
                                LCD_W, FULL_H);
    if (!lcd_tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return 1;
    }

    /* buttons[] array is defined in x48_sdl.c */

    if (test_mode)
    {
        char test_title[64];
        snprintf(test_title, sizeof(test_title), "MAC48GX %s — TEST MODE", APP_VERSION);
        SDL_SetWindowTitle(window, test_title);
    }

    /* Start emulator thread */
    if (pthread_create(&emu_thread, NULL, emulator_thread, NULL) != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(errno));
        return 1;
    }

    /* Launch test thread if --test mode */
    pthread_t test_thread;
    if (test_mode) {
        pthread_create(&test_thread, NULL, test_thread_func, NULL);
    }

    /* Cursors */
    SDL_Cursor *cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    SDL_Cursor *cursor_hand  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    int current_cursor_is_hand = 0;

    /* Overlay state */
    int show_keyboard_help = 0;
    int show_about = 0;

    /* --- SDL event loop --- */
    SDL_Event ev;
    int running = 1;

    /* Initial render */
    render(renderer, lcd_tex);

    while (running) {
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                exit_state = 0;
                break;

            case SDL_MOUSEMOTION: {
                /* Change cursor to hand when hovering over buttons */
                int b = hit_test(ev.motion.x, ev.motion.y);
                if (b >= 0 && !current_cursor_is_hand) {
                    SDL_SetCursor(cursor_hand);
                    current_cursor_is_hand = 1;
                } else if (b < 0 && current_cursor_is_hand) {
                    SDL_SetCursor(cursor_arrow);
                    current_cursor_is_hand = 0;
                }
                break;
            }

            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    /* Dismiss overlays on click */
                    if (show_keyboard_help || show_about) {
                        show_keyboard_help = 0;
                        show_about = 0;
                        break;
                    }
                    int b = hit_test(ev.button.x, ev.button.y);
                    if (b >= 0) {
                        pressed_btn = b;
                        sdl_push_event(b + 1);
                        got_alarm = 1;
                        pthread_mutex_lock(&uiConditionMutex);
                        pthread_cond_signal(&uiConditionVariable);
                        pthread_mutex_unlock(&uiConditionMutex);
                    }
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT && pressed_btn >= 0) {
                    sdl_push_event(pressed_btn + 100);
                    got_alarm = 1;
                    pthread_mutex_lock(&uiConditionMutex);
                    pthread_cond_signal(&uiConditionVariable);
                    pthread_mutex_unlock(&uiConditionMutex);
                    pressed_btn = -1;
                }
                break;

            case SDL_KEYDOWN: {
                SDL_Keymod mod = SDL_GetModState();
                int cmd = (mod & KMOD_GUI);  /* Cmd key on Mac */

                /* Cmd+Q = quit */
                if (cmd && ev.key.keysym.sym == SDLK_q) {
                    running = 0;
                    exit_state = 0;
                    break;
                }

                /* Cmd+C = copy stack level 1 to clipboard */
                if (cmd && ev.key.keysym.sym == SDLK_c) {
                    char buf[65536];
                    if (read_stack_level(1, buf, sizeof(buf)) == 0) {
                        char *val = trim(buf);
                        SDL_SetClipboardText(val);
                    }
                    break;
                }

                /* Cmd+V = paste number from clipboard */
                if (cmd && ev.key.keysym.sym == SDLK_v) {
                    char *clip = SDL_GetClipboardText();
                    if (clip && clip[0]) {
                        const char *p;
                        for (p = clip; *p; p++) {
                            switch (*p) {
                            case '0': sdl_push_event(BTN_0+1); break;
                            case '1': sdl_push_event(BTN_1+1); break;
                            case '2': sdl_push_event(BTN_2+1); break;
                            case '3': sdl_push_event(BTN_3+1); break;
                            case '4': sdl_push_event(BTN_4+1); break;
                            case '5': sdl_push_event(BTN_5+1); break;
                            case '6': sdl_push_event(BTN_6+1); break;
                            case '7': sdl_push_event(BTN_7+1); break;
                            case '8': sdl_push_event(BTN_8+1); break;
                            case '9': sdl_push_event(BTN_9+1); break;
                            case '.': sdl_push_event(BTN_PERIOD+1); break;
                            case '-': sdl_push_event(BTN_NEG+1); break;
                            default: break;
                            }
                            /* Brief release after each */
                            sdl_push_event(100 + ((*p >= '0' && *p <= '9') ?
                                ((*p == '0') ? BTN_0 : BTN_1 + (*p - '1')) : BTN_PERIOD));
                        }
                        got_alarm = 1;
                        pthread_mutex_lock(&uiConditionMutex);
                        pthread_cond_signal(&uiConditionVariable);
                        pthread_mutex_unlock(&uiConditionMutex);
                    }
                    SDL_free(clip);
                    break;
                }

                /* Cmd+K = toggle keyboard shortcut overlay */
                if (cmd && ev.key.keysym.sym == SDLK_k) {
                    show_keyboard_help = !show_keyboard_help;
                    show_about = 0;
                    break;
                }

                /* Cmd+I = toggle about overlay */
                if (cmd && ev.key.keysym.sym == SDLK_i) {
                    show_about = !show_about;
                    show_keyboard_help = 0;
                    break;
                }

                /* Dismiss overlays on Escape */
                if (ev.key.keysym.sym == SDLK_ESCAPE && (show_keyboard_help || show_about)) {
                    show_keyboard_help = 0;
                    show_about = 0;
                    break;
                }

                /* Normal key → calculator button */
                int b = keysym_to_button(ev.key.keysym.sym);
                if (b >= 0 && !ev.key.repeat && !cmd) {
                    sdl_push_event(b + 1);
                    got_alarm = 1;
                    pthread_mutex_lock(&uiConditionMutex);
                    pthread_cond_signal(&uiConditionVariable);
                    pthread_mutex_unlock(&uiConditionMutex);
                }
                break;
            }

            case SDL_KEYUP: {
                int b = keysym_to_button(ev.key.keysym.sym);
                if (b >= 0) {
                    sdl_push_event(b + 100);
                    got_alarm = 1;
                    pthread_mutex_lock(&uiConditionMutex);
                    pthread_cond_signal(&uiConditionVariable);
                    pthread_mutex_unlock(&uiConditionMutex);
                }
                break;
            }
            }
        }

        if (!show_keyboard_help && !show_about) {
            render(renderer, lcd_tex);
        }

        /* Draw overlay if active (replaces normal render) */
        if (show_keyboard_help) {
            SDL_SetRenderDrawColor(renderer, 20, 25, 30, 255);
            SDL_RenderClear(renderer);
            /* Solid dark background for readability */
            SDL_SetRenderDrawColor(renderer, 20, 25, 30, 255);
            SDL_Rect overlay = { 10, 10, WIN_W - 20, WIN_H - 20 };
            fill_rounded_rect(renderer, 10, 10, WIN_W - 20, WIN_H - 20, 12);
            /* Border */
            SDL_SetRenderDrawColor(renderer, 100, 180, 160, 255);
            SDL_RenderDrawRect(renderer, &overlay);

            SDL_Color w  = { 240, 240, 240, 255 };
            SDL_Color g  = { 130, 220, 200, 255 };
            SDL_Color gy = { 160, 160, 170, 255 };
            int lx = 50, rx = WIN_W/2 + 20;
            int y = 50;

            draw_text_centered(renderer, font_btn_lg, "Keyboard Shortcuts", g, WIN_W/2, y, 0); y += 45;

            draw_text_left(renderer, font_btn_sm, "Calculator Keys", g, lx, y, 0); y += 30;
            draw_text_left(renderer, font_btn_sm, "0 - 9  .  SPC", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "Number entry", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "+  -  *  /", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "Arithmetic", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Enter", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "ENTER / DUP", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Backspace", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "Backspace / DROP", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Delete", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "DEL", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Escape", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "ON / CANCEL", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Arrow keys", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "Cursor / navigation", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "F1 - F6", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "Menu keys A - F", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "s  c  t", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "SIN  COS  TAN", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "a", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "ALPHA", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "e  /  n", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "EEX  /  +/- (negate)", gy, rx, y, 0); y += 35;

            draw_text_left(renderer, font_btn_sm, "App Commands", g, lx, y, 0); y += 30;
            draw_text_left(renderer, font_btn_sm, "Cmd + C", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "Copy stack to clipboard", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Cmd + V", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "Paste number", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Cmd + K", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "This help", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Cmd + I", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "About", gy, rx, y, 0); y += 26;
            draw_text_left(renderer, font_btn_sm, "Cmd + Q", w, lx, y, 0);
            draw_text_left(renderer, font_btn_sm, "Quit", gy, rx, y, 0); y += 40;

            draw_text_centered(renderer, font_btn_sm, "Click or press Esc to dismiss", gy, WIN_W/2, y, 0);
            SDL_RenderPresent(renderer);
        } else if (show_about) {
            SDL_SetRenderDrawColor(renderer, 20, 25, 30, 255);
            SDL_RenderClear(renderer);
            fill_rounded_rect(renderer, 60, WIN_H/2 - 140, WIN_W - 120, 280, 12);
            {
                SDL_Rect border = { 60, WIN_H/2 - 140, WIN_W - 120, 280 };
                SDL_SetRenderDrawColor(renderer, 100, 180, 160, 255);
                SDL_RenderDrawRect(renderer, &border);
            }

            SDL_Color w = { 240, 240, 240, 255 };
            SDL_Color g = { 130, 220, 200, 255 };
            SDL_Color gy = { 160, 160, 170, 255 };
            int y = WIN_H/2 - 100;
            {
                char ver[64];
                snprintf(ver, sizeof(ver), "MAC48GX %s", APP_VERSION);
                draw_text_centered(renderer, font_btn_lg, ver, g, WIN_W/2, y, 0);
            }
            y += 40;
            draw_text_centered(renderer, font_btn_sm, "Saturn CPU Emulator for macOS", w, WIN_W/2, y, 0); y += 30;
            draw_text_centered(renderer, font_btn_sm, "x48 by Eddie C. Dost (1994)", gy, WIN_W/2, y, 0); y += 24;
            draw_text_centered(renderer, font_btn_sm, "droid48 by Arnaud Brochard", gy, WIN_W/2, y, 0); y += 24;
            draw_text_centered(renderer, font_btn_sm, "macOS port: MAC48GX", gy, WIN_W/2, y, 0); y += 30;
            draw_text_centered(renderer, font_btn_sm, "Licensed under GPL-3.0", g, WIN_W/2, y, 0); y += 35;
            draw_text_centered(renderer, font_btn_sm, "Click or press Esc to dismiss", gy, WIN_W/2, y, 0);
            SDL_RenderPresent(renderer);
        }

        update_audio();
        SDL_Delay(16); /* ~60 fps */

        /* In test mode, exit when tests complete */
        if (test_mode && test_finished) {
            running = 0;
            exit_state = 0;
        }
    }

    exit_state = 0;
    /* Signal condition so emulator thread wakes and exits */
    pthread_mutex_lock(&uiConditionMutex);
    pthread_cond_broadcast(&uiConditionVariable);
    pthread_mutex_unlock(&uiConditionMutex);

    if (test_mode)
        pthread_join(test_thread, NULL);

    pthread_join(emu_thread, NULL);

    /* Save state */
    write_files();

    SDL_FreeCursor(cursor_arrow);
    SDL_FreeCursor(cursor_hand);
    if (audio_dev > 0)
        SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(lcd_tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (font_btn)    TTF_CloseFont(font_btn);
    if (font_btn_lg) TTF_CloseFont(font_btn_lg);
    if (font_btn_sm) TTF_CloseFont(font_btn_sm);
    if (font_shift)  TTF_CloseFont(font_shift);
    if (font_title)  TTF_CloseFont(font_title);
    TTF_Quit();
    SDL_Quit();

    return test_mode ? test_exit_code : 0;
}
