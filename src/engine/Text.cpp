#include "Text.h"

#include "Font.h"
#include "Framebuffer.h"

int textWidth(const char* text, int scale) {
    int n = 0;
    for (const char* p = text; *p; ++p) ++n;
    if (n == 0) return 0;
    // el ultimo caracter no arrastra separacion detras
    return (n * (FONT_W + FONT_GAP) - FONT_GAP) * scale;
}

int textHeight(int scale) { return FONT_H * scale; }

void drawText(Framebuffer& fb, int x, int y, const char* text, unsigned char color,
              int scale) {
    const uint8_t* glyphs = fontGlyphs();

    for (const char* p = text; *p; ++p) {
        const uint8_t* g = glyphs + fontIndex(*p) * FONT_H;
        for (int row = 0; row < FONT_H; ++row) {
            uint8_t bits = g[row];
            if (bits == 0) continue;  // fila vacia: se salta entera
            for (int col = 0; col < FONT_W; ++col) {
                if (!(bits & (1 << (FONT_W - 1 - col)))) continue;
                fb.fillRect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
        x += (FONT_W + FONT_GAP) * scale;
    }
}

void drawTextCentered(Framebuffer& fb, int y, const char* text, unsigned char color,
                      int scale) {
    drawText(fb, (fb.width() - textWidth(text, scale)) / 2, y, text, color, scale);
}

const char* intToText(char* buf, int value) {
    char tmp[12];
    int n = 0;
    bool negative = value < 0;
    // se trabaja en negativo para que INT_MIN no desborde al cambiarle el signo
    if (!negative) value = -value;
    do {
        tmp[n++] = char('0' - (value % 10));
        value /= 10;
    } while (value != 0);

    int i = 0;
    if (negative) buf[i++] = '-';
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = '\0';
    return buf;
}
