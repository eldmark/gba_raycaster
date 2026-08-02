#pragma once

#include <cstdint>
#include <vector>

// Buffer de color plano en ARGB8888.
//
// Este es el UNICO archivo del motor que se reescribe para GBA: alla el buffer
// pasa a ser el VRAM de modo 3/4 en RGB555. La interfaz se queda igual.
class Framebuffer {
public:
    Framebuffer(int w, int h);

    void clear(uint32_t color);
    void setPixel(int x, int y, uint32_t color);
    void fillRect(int x, int y, int w, int h, uint32_t color);

    const uint32_t* pixels() const { return buf_.data(); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_, height_;
    std::vector<uint32_t> buf_;
};

// Helpers de color, sin dependencias.
inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

// Multiplica el color por un factor [0,1] para sombrear por distancia.
inline uint32_t shade(uint32_t c, float f) {
    uint8_t r = uint8_t(((c >> 16) & 0xFF) * f);
    uint8_t g = uint8_t(((c >> 8) & 0xFF) * f);
    uint8_t b = uint8_t((c & 0xFF) * f);
    return rgb(r, g, b);
}
