
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>

#define MIN_W  80 /* chars */
#define MIN_H  0  /* chars */
#define MARGIN 1  /* px */

#define DIE(...)                      \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
        exit(1);                      \
    }

enum EPaletteIndexes {
    COL_BACK,

    PALETTE_SZ,
};

typedef struct {
    uint8_t r, g, b, a;
} Color;

static Color palette[PALETTE_SZ];

static uint32_t w = MIN_W, h = MIN_H;

/* Actually png_bytep is typedef'd to a pointer, so this is a (void**) */
static png_bytep* rows = NULL;

/*----------------------------------------------------------------------------*/

static inline void setup_palette(void) {
    palette[COL_BACK] = (Color){ 34, 34, 34, 255 };
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

static void clear_image(Color c) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w * 4; x += 4) {
            rows[y][x]     = c.r;
            rows[y][x + 1] = c.g;
            rows[y][x + 2] = c.b;
            rows[y][x + 3] = c.a;
        }
    }
}

static void write_png_file(char* filename) {
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
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    /* Write the rows, a global variable that has been filled somewhere else */
    png_write_image(png, rows);
    png_write_end(png, NULL);

    /* Free each pointer of the rows pointer array */
    for (uint32_t y = 0; y < h; y++)
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

    /* Add top, bottom, left and down margins */
    w += MARGIN * 2;
    h += MARGIN * 2;

    /* We allocate H rows, W cols in each row, and 4 bytes per pixel */
    rows = malloc(h * sizeof(png_bytep));
    for (uint32_t y = 0; y < h; y++)
        rows[y] = malloc(w * sizeof(uint8_t) * 4);

    /* Clear with background */
    clear_image(palette[COL_BACK]);

    write_png_file(argv[2]);

    puts("Done.");
    return 0;
}
