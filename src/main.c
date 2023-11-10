
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <png.h>

#include "fonts/main_font.h" /* FONT_W, FONT_H, main_font[] */

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

#define COL(R, G, B, A) ((Color){ .r = R, .g = G, .b = B, .a = A })

#define DIE(...)                      \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
        exit(1);                      \
    }

enum EPaletteIndexes {
    COL_BACK,
    COL_BORDER,
    COL_DEFAULT,
    COL_COMMENT,

    PALETTE_SZ,
};

enum ECommentTypes {
    COMMENT_NO,    /* We are not in a comment */
    COMMENT_LINE,  /* Single line */
    COMMENT_MULTI, /* Multi-line */
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

/* What kind of comment are we in? */
static enum ECommentTypes in_comment = COMMENT_NO;

/* Actually png_bytep is typedef'd to a pointer, so this is a (void**) */
static png_bytep* rows = NULL;

/*----------------------------------------------------------------------------*/

static inline bool get_font_bit(uint8_t c, uint8_t x, uint8_t y) {
    return main_font[c * FONT_H + y] & (0x80 >> x);
}

static inline void setup_palette(void) {
    palette[COL_BACK]    = COL(10, 10, 10, 255);
    palette[COL_BORDER]  = COL(40, 40, 40, 255);
    palette[COL_DEFAULT] = COL(255, 255, 255, 255);
    palette[COL_COMMENT] = COL(152, 152, 152, 255);
}

static void input_get_dimensions(char* filename) {
    FILE* fd = fopen(filename, "r");

    uint32_t x = 0, y = 0;

    char c;
    while ((c = fgetc(fd)) != EOF) {
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

    fclose(fd);
}

static void draw_rect(int x, int y, int w, int h, Color c) {
    for (int cur_y = y; cur_y < y + h; cur_y++) {
        for (int cur_x = x * 4; cur_x < (x + w) * 4; cur_x += 4) {
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

static void png_print(const char* s, Color fg, Color bg) {
    while (*s != '\0' && *s != EOF) {
        png_putchar(*s, fg, bg);
        s++;
    }
}

static bool closes_comment(const char* token) {
    /* Get the position of the NULL terminator */
    int end = 0;
    while (token[end] != '\0')
        end++;

    /* Check if the string ends a multi-line comment */
    return (end >= 2 && token[end - 2] == '*' && token[end - 1] == '/');
}

static void get_colors(const char* token, Color* fg, Color* bg) {
    /* Checked from source_to_png(), ignore */
    if (in_comment == COMMENT_LINE) {
        *fg = palette[COL_COMMENT];
        *bg = palette[COL_BACK];
        return;
    }

    /* We need an extra variable because we might open and end the comment in
     * the same word */
    bool comment_end = closes_comment(token);

    /* Asumes all comments are separated from the code by a space */
    if (token[0] == '/') {
        if (token[1] == '/') {
            in_comment = COMMENT_LINE;
            *fg        = palette[COL_COMMENT];
            *bg        = palette[COL_BACK];
            return;
        }

        // Single-line comment
        if (token[1] == '*') {
            *fg = palette[COL_COMMENT];
            *bg = palette[COL_BACK];

            /* Starts and end a comment at the same time? */
            if (!comment_end)
                in_comment = COMMENT_MULTI;

            return;
        }
    }

    if (comment_end) {
        *fg        = palette[COL_COMMENT];
        *bg        = palette[COL_BACK];
        in_comment = COMMENT_NO;
        return;
    }

    /* We couldn't check this earlier because we had to check if we closed the
     * multi-line comment */
    if (in_comment == COMMENT_MULTI) {
        *fg = palette[COL_COMMENT];
        *bg = palette[COL_BACK];
        return;
    }

    *fg = palette[COL_DEFAULT];
    *bg = palette[COL_BACK];
}

static void source_to_png(const char* filename) {
    /* Write the characters to the rows array */
    FILE* fd = fopen(filename, "r");
    if (!fd)
        DIE("Can't open file: \"%s\"\n", filename);

    /* No word is going to be larger than a line */
    char* word_buf   = calloc(w, sizeof(char));
    int word_buf_pos = 0;

    char c;
    while ((c = fgetc(fd)) != EOF) {
        if (!isspace(c)) {
            word_buf[word_buf_pos++] = c;
            continue;
        }

        /* We encountered a word separator, terminate string */
        word_buf[word_buf_pos] = '\0';

        /* Check color and print */
        if (word_buf_pos > 0) {
            Color fg, bg;
            get_colors(word_buf, &fg, &bg);
            png_print(word_buf, fg, bg);

            /* Reset for new word */
            word_buf_pos = 0;
        }

        /* We were in an single line comment and we changed line */
        if (c == '\n' && in_comment == COMMENT_LINE)
            in_comment = COMMENT_NO;

        /* Print the word separator we encountered */
        png_putchar(c, palette[COL_DEFAULT], palette[COL_BACK]);
    }

    free(word_buf);
    fclose(fd);
}

static void draw_border(void) {
    draw_rect(0, 0, w_px, BORDER_SZ, palette[COL_BORDER]);
    draw_rect(0, 0, BORDER_SZ, h_px, palette[COL_BORDER]);
    draw_rect(0, h_px - BORDER_SZ, w_px, BORDER_SZ, palette[COL_BORDER]);
    draw_rect(w_px - BORDER_SZ, 0, BORDER_SZ, h_px, palette[COL_BORDER]);
}

static void write_png_file(const char* filename) {
    FILE* fd = fopen(filename, "wb");
    if (!fd)
        DIE("Can't open file: \"%s\"\n", filename);

    png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
        DIE("Can't create png_structp\n");

    png_infop info = png_create_info_struct(png);
    if (!info)
        DIE("Can't create png_infop\n");

    /* Specify the PNG info */
    png_init_io(png, fd);
    png_set_IHDR(png, info, w_px, h_px, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
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

    fclose(fd);
    png_destroy_write_struct(&png, &info);
}

int main(int argc, char** argv) {
    if (argc < 3)
        DIE("Usage: %s <in> <out>\n", argv[0]);

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
        rows[y] = malloc(w_px * sizeof(uint8_t) * 4);

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
