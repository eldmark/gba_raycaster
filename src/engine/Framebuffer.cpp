#include "Framebuffer.h"

#include <algorithm>

Framebuffer::Framebuffer(int w, int h)
    : width_(w), height_(h), owned_(size_t(w) * size_t(h), 0), buf_(owned_.data()) {}

Framebuffer::Framebuffer(int w, int h, uint8_t* externalBuf)
    : width_(w), height_(h), owned_(), buf_(externalBuf) {}

void Framebuffer::clear(uint8_t color) {
    std::fill(buf_, buf_ + size_t(width_) * size_t(height_), color);
}

void Framebuffer::setPixel(int x, int y, uint8_t color) {
    // fuera de rango se descarta en silencio: el raycaster dibuja columnas
    // que se salen de la pantalla y cuenta con esto.
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    buf_[size_t(y) * size_t(width_) + size_t(x)] = color;
}

void Framebuffer::fillRect(int x, int y, int w, int h, uint8_t color) {
    // recortamos primero y luego llenamos por filas: esto se llama una vez
    // por columna de pantalla, no conviene pasar por setPixel pixel a pixel.
    int x0 = std::max(x, 0);
    int y0 = std::max(y, 0);
    int x1 = std::min(x + w, width_);
    int y1 = std::min(y + h, height_);
    if (x0 >= x1 || y0 >= y1) return;

    for (int row = y0; row < y1; ++row) {
        uint8_t* begin = buf_ + (size_t(row) * size_t(width_) + size_t(x0));
        std::fill(begin, begin + (x1 - x0), color);
    }
}

void Framebuffer::shiftRow(int y, int amount) {
    if (y < 0 || y >= height_ || width_ <= 1) return;
    amount %= width_;
    if (amount < 0) amount += width_;
    if (amount == 0) return;
    uint8_t* begin = buf_ + size_t(y) * size_t(width_);
    std::rotate(begin, begin + amount, begin + width_);
}
