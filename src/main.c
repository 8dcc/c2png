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

/*----------------------------------------------------------------------------*/
/* Macros */

#define MIN_W        70 /* chars */
#define MIN_H        0  /* chars */
#define MARGIN       10 /* pixels */
#define LINE_SPACING 1  /* pixels */
#define BORDER_SZ    2  /* pixels */
#define TAB_SZ       4  /* chars */

/* Size of each entry in the 'Ctx.rows' array, in bytes */
#define COL_SZ 4

/*----------------------------------------------------------------------------*/
/* Callable macros */

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

/*----------------------------------------------------------------------------*/
/* Structures and enums */

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

typedef struct {
    /*
     * Current position when printing in chars.
     */
    uint32_t x, y;

    /*
     * Input dimensions in chars
     */
    uint32_t w, h;

    /*
     * Size of the image in pixels, including margins, border, etc.
     */
    uint32_t w_px, h_px;

    /*
     * PNG rows. Note that 'png_bytep' is typedef'd to a pointer, so this is
     * actually a (void**).
     */
    png_bytep* rows;

    /*
     * Color palette, see 'EPaletteIndexes' enum above.
     */
    Color palette[PALETTE_SZ];
} Ctx;

/*----------------------------------------------------------------------------*/
/* Miscellaneous helpers */

static inline void* safe_malloc(size_t size) {
    void* result = malloc(size);
    if (result == NULL)
        DIE("Error when allocating %zu bytes: %s", size, strerror(errno));
    return result;
}

static inline FILE* safe_fopen(const char* pathname, const char* mode) {
    FILE* fp = fopen(pathname, mode);
    if (fp == NULL)
        DIE("Can't open file '%s': %s", pathname, strerror(errno));
    return fp;
}

/*----------------------------------------------------------------------------*/
/* Initialization and deinitialization functions */

/*
 * Read the number of rows and columns from the specified file, and store them
 * in the specified context.
 */
static void get_file_dimensions(Ctx* ctx, FILE* fp) {
    /*
     * Store old offset in the file, for restoring it later. Then, move to the
     * beginning of the file.
     */
    const long old_offset = ftell(fp);
    if (old_offset == -1)
        DIE("Error getting file offset: %s", strerror(errno));
    rewind(fp);

    uint32_t x = 0, y = 0;
    char c;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') {
            y++;
            x = 0;
        } else {
            x++;
        }

        if (ctx->w < x)
            ctx->w = x;

        if (ctx->h < y)
            ctx->h = y;
    }

    fseek(fp, old_offset, SEEK_SET);
}

/*
 * Initialize the specified context with the information of the specified input
 * file.
 */
static void ctx_init(Ctx* ctx, FILE* input_fp) {
    ctx->palette[COL_DEFAULT]   = COL(0xFFFFFF, 255);
    ctx->palette[COL_PREPROC]   = COL(0xFF6740, 255);
    ctx->palette[COL_TYPES]     = COL(0x79A8FF, 255);
    ctx->palette[COL_KWRDS]     = COL(0xFF6F9F, 255);
    ctx->palette[COL_NUMBER]    = COL(0x88CA9F, 255);
    ctx->palette[COL_STRING]    = COL(0x00D3D0, 255);
    ctx->palette[COL_COMMENT]   = COL(0x989898, 255);
    ctx->palette[COL_FUNC_CALL] = ctx->palette[COL_DEFAULT];
    ctx->palette[COL_SYMBOL]    = ctx->palette[COL_DEFAULT];
    ctx->palette[COL_BACK]      = COL(0x050505, 255);
    ctx->palette[COL_BORDER]    = COL(0x222222, 255);

    /*
     * Will be used in 'putchar2png'.
     */
    ctx->x = 0;
    ctx->y = 0;

    /*
     * Fill the 'w' and 'h' members of the context.
     */
    ctx->w = MIN_W;
    ctx->h = MIN_H;
    get_file_dimensions(ctx, input_fp);

    /*
     * Convert character dimensions to pixel size, adding top, bottom, left and
     * down margins.
     */
    ctx->w_px = MARGIN + ctx->w * FONT_W + MARGIN;
    ctx->h_px = MARGIN + ctx->h * (FONT_H + LINE_SPACING) + MARGIN;

    /*
     * Allocate 'ctx->h_px' rows, 'ctx->w_px' cols in each row, and 4 bytes per
     * pixel.
     */
    ctx->rows = safe_malloc(ctx->h_px * sizeof(png_bytep));
    for (uint32_t y = 0; y < ctx->h_px; y++)
        ctx->rows[y] = safe_malloc(ctx->w_px * sizeof(uint8_t) * COL_SZ);
}

/*
 * Deinitialize a 'Ctx' structure, freeing the relevant members. Does not free
 * the 'Ctx' pointer itself.
 */
static void ctx_finish(Ctx* ctx) {
    /*
     * Free each pointer in the rows array, and the pointer to the array itself.
     */
    for (uint32_t y = 0; y < ctx->h_px; y++)
        free(ctx->rows[y]);
    free(ctx->rows);
}

/*----------------------------------------------------------------------------*/
/* PNG printing functions */

/*
 * Check if the specified coordinates in the main font's bitmap are set for the
 * specified character.
 */
static inline bool get_font_bit(uint8_t c, uint8_t x, uint8_t y) {
    return main_font[c * FONT_H + y] & (0x80 >> x);
}

/*
 * Write the specified character with the specified foreground and background
 * colors to the internal PNG representation in the specified context.
 */
static void putchar2png(Ctx* ctx, char c, Color fg, Color bg) {
    /*
     * Handle special cases.
     *
     * TODO: Don't handle newline here, add a 'newline2png' function, replace in
     * 'file2png'.
     */
    switch (c) {
        case '\n':
            ctx->y++;
            ctx->x = 0;
            return;
        case '\t':
            for (int i = 0; i < TAB_SZ; i++)
                putchar2png(ctx, ' ', fg, bg);
            return;
        default:
            break;
    }

    /*
     * Iterate each row and column of the bitmap associated to current character
     * in the font.
     */
    for (uint8_t fy = 0; fy < FONT_H; fy++) {
        /*
         * Get real position of this character in the image. This is obtained by
         * converting the current Y offset in characters ('ctx->y') to the
         * offset in pixels in the image, and also adding the current pixel
         * offset in the font ('fy').
         */
        const uint32_t final_y = CHAR_Y_TO_PX(ctx->y) + fy;

        for (uint8_t fx = 0; fx < FONT_W; fx++) {
            /*
             * Just like we did with 'final_y', but we also need to multiply the
             * value by the size of each pixel in the column array.
             */
            const uint32_t final_x = (CHAR_X_TO_PX(ctx->x) + fx) * COL_SZ;

            /*
             * Actual color to use depending if the bit is set in the font.
             */
            const Color col = (get_font_bit(c, fx, fy)) ? fg : bg;

            ctx->rows[final_y][final_x]     = col.r;
            ctx->rows[final_y][final_x + 1] = col.g;
            ctx->rows[final_y][final_x + 2] = col.b;
            ctx->rows[final_y][final_x + 3] = col.a;
        }
    }

    ctx->x++;
}

/*
 * Print the specified (optionally highlighted) line to the specified PNG
 * context.
 */
static void print2png(Ctx* ctx, const char* s) {
    Color fg = ctx->palette[COL_DEFAULT];
    Color bg = ctx->palette[COL_BACK];

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
            fg = ctx->palette[fg_idx];
            bg = ctx->palette[bg_idx];
#endif

            continue;
        }

        putchar2png(ctx, *s, fg, bg);
        s++;
    }
}

/*
 * Highlight each line in the specified file, and print them to the specified
 * PNG context.
 */
static void file2png(Ctx* ctx, FILE* fp) {
    /*
     * Store old offset in the file, for restoring it later. Then, move to the
     * beginning of the file.
     */
    const long old_offset = ftell(fp);
    if (old_offset == -1)
        DIE("Error getting file offset: %s", strerror(errno));
    rewind(fp);

    if (highlight_init(NULL) < 0)
        DIE("Unable to initialize the highlight library.");

    /* Line buffer used when calling 'highlight_line' */
    char* hl_line = highlight_alloc_line();

    /* Line buffer for storing each line, and adding a null terminator */
    const size_t line_buf_sz = ctx->w + 1;
    size_t line_buf_pos      = 0;
    char* line_buf           = safe_malloc(line_buf_sz * sizeof(char));

    char c;
    while ((c = fgetc(fp)) != EOF) {
        assert(line_buf_pos < line_buf_sz);

        /* Store characters in the buffer until we encounter a newline */
        if (c != '\n') {
            line_buf[line_buf_pos++] = c;
            continue;
        }

        /*
         * We encountered newline, then:
         *
         *   1. Terminate the string.
         *   2. Highlight the line.
         *   3. Print the highlighted line (with ANSI escape sequences) into the
         *      internal PNG structure.
         *   4. Print the newline to the PNG structure, effectively changing the
         *      line.
         *   5. Reset the current position in the line to zero.
         */
        line_buf[line_buf_pos] = '\0';
        hl_line = highlight_line(line_buf, hl_line, line_buf_pos);
        print2png(ctx, hl_line);
        putchar2png(ctx,
                    '\n',
                    ctx->palette[COL_DEFAULT],
                    ctx->palette[COL_BACK]);
        line_buf_pos = 0;
    }

    highlight_free(hl_line);
    highlight_finish();

    free(line_buf);
    fseek(fp, old_offset, SEEK_SET);
}

/*----------------------------------------------------------------------------*/
/* PNG drawing functions */

/*
 * Draw a rectangle in the specified PNG context.
 */
static inline void draw_rect(Ctx* ctx, int x, int y, int w, int h, Color c) {
    for (int cur_y = y; cur_y < y + h; cur_y++) {
        /*
         * To get the real position in the rows array, we need to multiply the
         * positions by the size of each element (i.e. COL_SZ, which is 4).
         */
        for (int cur_x = x * COL_SZ; cur_x < (x + w) * COL_SZ;
             cur_x += COL_SZ) {
            ctx->rows[cur_y][cur_x]     = c.r;
            ctx->rows[cur_y][cur_x + 1] = c.g;
            ctx->rows[cur_y][cur_x + 2] = c.b;
            ctx->rows[cur_y][cur_x + 3] = c.a;
        }
    }
}

/*
 * Draw a border of 'BORDER_SZ' pixels to the specified PNG context.
 */
static void draw_border(Ctx* ctx) {
    draw_rect(ctx, 0, 0, ctx->w_px, BORDER_SZ, ctx->palette[COL_BORDER]);
    draw_rect(ctx, 0, 0, BORDER_SZ, ctx->h_px, ctx->palette[COL_BORDER]);
    draw_rect(ctx,
              0,
              ctx->h_px - BORDER_SZ,
              ctx->w_px,
              BORDER_SZ,
              ctx->palette[COL_BORDER]);
    draw_rect(ctx,
              ctx->w_px - BORDER_SZ,
              0,
              BORDER_SZ,
              ctx->h_px,
              ctx->palette[COL_BORDER]);
}

/*----------------------------------------------------------------------------*/
/* PNG writing functions */

/*
 * Write the internal PNG representation of the specified context to the
 * specified file.
 */
static void png2file(Ctx* ctx, FILE* fp) {
    png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
        DIE("Can't create `png_structp'.");

    png_infop info = png_create_info_struct(png);
    if (!info)
        DIE("Can't create `png_infop'.");

    /*
     * Specify the PNG info, including the output file.
     */
    png_init_io(png, fp);
    png_set_IHDR(png,
                 info,
                 ctx->w_px,
                 ctx->h_px,
                 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    /*
     * Write the rows from the context into the PNG file.
     */
    png_write_image(png, ctx->rows);
    png_write_end(png, NULL);

    png_destroy_write_struct(&png, &info);
}

/*----------------------------------------------------------------------------*/
/* Entry point */

int main(int argc, char** argv) {
    if (argc < 3)
        DIE("Usage: %s INPUT.c OUTPUT.png", argv[0]);

    const char* input_path  = argv[1];
    const char* output_path = argv[2];
    FILE* input_fp          = safe_fopen(input_path, "r");
    FILE* output_fp         = safe_fopen(output_path, "wb");

    Ctx ctx;
    ctx_init(&ctx, input_fp);
    printf("Source contains %d rows and %d cols.\n", ctx.h, ctx.w);

    /*
     * Clear the whole image with the background.
     */
    draw_rect(&ctx, 0, 0, ctx.w_px, ctx.h_px, ctx.palette[COL_BACK]);

    /*
     * Convert the source file to an internal PNG data structure. Then, draw the
     * border on top of the converted image.
     */
    printf("Generating internal %dx%d image...\n", ctx.w_px, ctx.h_px);
    file2png(&ctx, input_fp);
    draw_border(&ctx);

    /*
     * Write the internal PNG structure ('Ctx->rows') to the output PNG file.
     */
    printf("Generating PNG image and writing to '%s'...\n", output_path);
    png2file(&ctx, output_fp);
    puts("Done.");

    ctx_finish(&ctx);
    fclose(output_fp);
    fclose(input_fp);
    return 0;
}
