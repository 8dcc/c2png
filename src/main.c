/*
 * Copyright 2025 8dcc
 *
 * This file is part of c2png.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <png.h>

#include "fonts/main_font.h" /* FONT_W, FONT_H, main_font[] */
#include "include/highlight.h"

#define MIN_W        80 /* chars */
#define MIN_H        0  /* chars */
#define MARGIN       10 /* px */
#define LINE_SPACING 1  /* px */
#define BORDER_SZ    2  /* px */
#define TAB_SZ       4  /* chars */

/* Bytes of each entry in rows[] */
#define COL_SZ 4

/* Character position -> Pixel position */
#define CHAR_Y_TO_PX(Y) (MARGIN + Y * (FONT_H + LINE_SPACING))
#define CHAR_X_TO_PX(X) (MARGIN + X * FONT_W)

#define COL(RGB, A)                                                            \
    ((Color){                                                                  \
      .r = (RGB >> 16) & 0xFF,                                                 \
      .g = (RGB >> 8) & 0xFF,                                                  \
      .b = RGB & 0xFF,                                                         \
      .a = A & 0xFF,                                                           \
    })

#define DIE(...)                                                               \
    do {                                                                       \
        fprintf(stderr, __VA_ARGS__);                                          \
        fputc('\n', stderr);                                                   \
        exit(1);                                                               \
    } while (0)

enum EPaletteIndexes {
    /* Used by highlight.c */
    COL_DEFAULT = 0,
    COL_PREPROC,
    COL_TYPES,
    COL_KWRDS,
    COL_NUMBER,
    COL_STRING,
    COL_COMMENT,
    COL_FUNC_CALL,
    COL_SYMBOL,

    /* Used only in main.c */
    COL_BACK,
    COL_BORDER,

    PALETTE_SZ,
};

typedef struct {
    uint8_t r, g, b, a;
} Color;

/*----------------------------------------------------------------------------*/

/* Initialized in setup_palette() */
static Color palette[PALETTE_SZ];

/* Current position when printing in chars */
static uint32_t x = 0, y = 0;

/* Size in chars. Overwritten by input_get_dimensions() */
static uint32_t w = MIN_W, h = MIN_H;

/* Size in px. Includes margins */
static uint32_t w_px = 0, h_px = 0;

/* Actually png_bytep is typedef'd to a pointer, so this is a (void**) */
static png_bytep* rows = NULL;

/*----------------------------------------------------------------------------*/

static inline bool get_font_bit(uint8_t c, uint8_t x, uint8_t y) {
    return main_font[c * FONT_H + y] & (0x80 >> x);
}

static inline void setup_palette(void) {
    palette[COL_DEFAULT]   = COL(0xFFFFFF, 255);
    palette[COL_PREPROC]   = COL(0xFF6740, 255);
    palette[COL_TYPES]     = COL(0x79A8FF, 255);
    palette[COL_KWRDS]     = COL(0xFF6F9F, 255);
    palette[COL_NUMBER]    = COL(0x88CA9F, 255);
    palette[COL_STRING]    = COL(0x00D3D0, 255);
    palette[COL_COMMENT]   = COL(0x989898, 255);
    palette[COL_FUNC_CALL] = palette[COL_DEFAULT];
    palette[COL_SYMBOL]    = palette[COL_DEFAULT];

    palette[COL_BACK]   = COL(0x050505, 255);
    palette[COL_BORDER] = COL(0x222222, 255);
}

static void input_get_dimensions(char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp)
        DIE("Can't open file '%s': %s", filename, strerror(errno));

    uint32_t x = 0, y = 0;

    char c;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') {
            y++;
            x = 0;
        } else {
            x++;
        }

        if (w < x)
            w = x;

        if (h < y)
            h = y;
    }

    fclose(fp);
}

static void draw_rect(int x, int y, int w, int h, Color c) {
    for (int cur_y = y; cur_y < y + h; cur_y++) {
        /* To get the real position in the rows array, we need to multiply the
         * positions by the size of each element: COL_SZ (4) */
        for (int cur_x = x * COL_SZ; cur_x < (x + w) * COL_SZ;
             cur_x += COL_SZ) {
            rows[cur_y][cur_x]     = c.r;
            rows[cur_y][cur_x + 1] = c.g;
            rows[cur_y][cur_x + 2] = c.b;
            rows[cur_y][cur_x + 3] = c.a;
        }
    }
}

static void png_putchar(char c, Color fg, Color bg) {
    /* Hadle special cases */
    switch (c) {
        case '\n':
            y++;
            x = 0;
            return;
        case '\t':
            for (int i = 0; i < TAB_SZ; i++)
                png_putchar(' ', fg, bg);
            return;
    }

    /* Iterate each pixel that forms the font char */
    for (uint8_t fy = 0; fy < FONT_H; fy++) {
        /* Get real screen position from the char offset on the image and the
         * pixel font offset on the char */
        const uint32_t final_y = CHAR_Y_TO_PX(y) + fy;

        for (uint8_t fx = 0; fx < FONT_W; fx++) {
            /* For the final_x, we also need to multiply it by the size of each
            pixel in the cols array */
            const uint32_t final_x = (CHAR_X_TO_PX(x) + fx) * COL_SZ;

            /* Actual color to use depending if the bit is set in the font */
            Color col = get_font_bit(c, fx, fy) ? fg : bg;

            rows[final_y][final_x]     = col.r;
            rows[final_y][final_x + 1] = col.g;
            rows[final_y][final_x + 2] = col.b;
            rows[final_y][final_x + 3] = col.a;
        }
    }

    x++;
}

static void png_print(const char* s) {
    Color fg = palette[COL_DEFAULT];
    Color bg = palette[COL_BACK];

    while (*s != '\0' && *s != EOF) {
        /* Escape character used to change color */
        if (*s == 0x1B) {
            s++;

            /* See bottom of COLORS[] in highlight.c */
            const int fg_idx = *s++;
            const int bg_idx = *s++;

            /* Also skip NULL terminator for the color strings */
            s++;

#ifdef DISABLE_SYNTAX_HIGHLIGHT
            /* No syntax highlight, unused */
            (void)fg_idx;
            (void)bg_idx;
#else
            fg = palette[fg_idx];
            bg = palette[bg_idx];
#endif

            continue;
        }

        png_putchar(*s, fg, bg);
        s++;
    }
}

static void source_to_png(const char* filename) {
    /* Write the characters to the rows array */
    FILE* fp = fopen(filename, "r");
    if (!fp)
        DIE("Can't open file '%s': %s", filename, strerror(errno));

    if (highlight_init(NULL) < 0)
        DIE("Unable to initialize the highlight library.");

    /* Used when calling highlight_line() */
    char* hl_line = highlight_alloc_line();

    /* Used by us for storing each line and adding NULL terminator */
    size_t line_buf_sz  = w + 1;
    size_t line_buf_pos = 0;
    char* line_buf      = malloc(line_buf_sz * sizeof(char));

    char c;
    while ((c = fgetc(fp)) != EOF) {
        assert(line_buf_pos < line_buf_sz);

        /* Store chars until newline */
        if (c != '\n') {
            line_buf[line_buf_pos++] = c;
            continue;
        }

        /* We encountered newline, terminate string */
        line_buf[line_buf_pos] = '\0';

        /* Check color and print */
        hl_line = highlight_line(line_buf, hl_line, line_buf_pos);

        /* Print the line with the escape codes, used for changing the colors */
        png_print(hl_line);

        /* Reset for next line */
        line_buf_pos = 0;

        /* Print the newline we encountered */
        png_putchar('\n', palette[COL_DEFAULT], palette[COL_BACK]);
    }

    highlight_free(hl_line);
    highlight_finish();

    free(line_buf);
    fclose(fp);
}

static void draw_border(void) {
    draw_rect(0, 0, w_px, BORDER_SZ, palette[COL_BORDER]);
    draw_rect(0, 0, BORDER_SZ, h_px, palette[COL_BORDER]);
    draw_rect(0, h_px - BORDER_SZ, w_px, BORDER_SZ, palette[COL_BORDER]);
    draw_rect(w_px - BORDER_SZ, 0, BORDER_SZ, h_px, palette[COL_BORDER]);
}

static void write_png_file(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp)
        DIE("Can't open file '%s': %s", filename, strerror(errno));

    png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
        DIE("Can't create `png_structp'.");

    png_infop info = png_create_info_struct(png);
    if (!info)
        DIE("Can't create `png_infop'.");

    /* Specify the PNG info */
    png_init_io(png, fp);
    png_set_IHDR(png,
                 info,
                 w_px,
                 h_px,
                 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    /* Write the rows, a global variable that has been filled somewhere else */
    png_write_image(png, rows);
    png_write_end(png, NULL);

    /* Free each pointer of the rows pointer array */
    for (uint32_t y = 0; y < h_px; y++)
        free(rows[y]);

    /* And the array itself */
    free(rows);

    fclose(fp);
    png_destroy_write_struct(&png, &info);
}

int main(int argc, char** argv) {
    if (argc < 3)
        DIE("Usage: %s INPUT.c OUTPUT.png", argv[0]);

    /* Setup color palette */
    setup_palette();

    /* Ugly, but does the job */
    input_get_dimensions(argv[1]);
    printf("Source contains %d rows and %d cols.\n", h, w);

    /* Convert to pixel size, adding top, bottom, left and down margins */
    w_px = MARGIN + w * FONT_W + MARGIN;
    h_px = MARGIN + h * (FONT_H + LINE_SPACING) + MARGIN;
    printf("Generating %dx%d image...\n", w_px, h_px);

    /* We allocate H_PX rows, W_PX cols in each row, and 4 bytes per pixel */
    rows = malloc(h_px * sizeof(png_bytep));
    for (uint32_t y = 0; y < h_px; y++)
        rows[y] = malloc(w_px * sizeof(uint8_t) * COL_SZ);

    /* Clear with background */
    draw_rect(0, 0, w_px, h_px, palette[COL_BACK]);

    /* Convert the text to png */
    source_to_png(argv[1]);

    /* Draw border */
    draw_border();

    /* Write rows to the output png file */
    write_png_file(argv[2]);

    puts("Done.");
    return 0;
}
