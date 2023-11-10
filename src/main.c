
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>

#include "fonts/main_font.h" /* FONT_W, FONT_H, main_font[] */

#define MIN_W        80 /* chars */
#define MIN_H        0  /* chars */
#define MARGIN       10 /* px */
#define LINE_SPACING 1  /* px */
#define BORDER_SZ    2  /* px */

/* Bytes of each entry in rows[] */
#define COL_SZ 4

/* Character position -> Pixel position */
#define CHAR_Y_TO_PX(Y) (MARGIN + Y * (FONT_H + LINE_SPACING))
#define CHAR_X_TO_PX(X) (MARGIN + X * FONT_W)

#define DIE(...)                      \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
        exit(1);                      \
    }

enum EPaletteIndexes {
    COL_BACK,
    COL_BORDER,
    COL_DEFAULT,

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
    palette[COL_BACK]    = (Color){ 10, 10, 10, 255 };
    palette[COL_BORDER]  = (Color){ 40, 40, 40, 255 };
    palette[COL_DEFAULT] = (Color){ 255, 255, 255, 255 };
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
    if (c == '\n') {
        y++;
        x = 0;
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

static void source_to_png(const char* filename) {
    /* Write the characters to the rows array */
    FILE* fd = fopen(filename, "r");
    if (!fd)
        DIE("Can't open file: \"%s\"\n", filename);

    /* TODO: Syntax */
    char c;
    while ((c = fgetc(fd)) != EOF)
        png_putchar(c, palette[COL_DEFAULT], palette[COL_BACK]);
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
