#pragma once

#include <cstdint>
#include <vector>

// Buffer indexado de 8 bits: cada pixel es un indice a la paleta de Textures.h,
// no un color. Es el mismo formato del modo 4 de la GBA.
//
// Este es el UNICO archivo del motor que se reescribe para GBA: alla el buffer
// pasa a ser el VRAM en 0x6000000 y la paleta se copia a BG_PALETTE. La
// interfaz se queda igual.
class Framebuffer {
public:
    // Escritorio: reserva su propio buffer en el heap.
    Framebuffer(int w, int h);

    // GBA: apunta a memoria que ya existe (un array estatico en EWRAM), sin
    // reservar nada. Es el cambio que pide el port: EWRAM son 256 KB y un
    // vector aqui solo arrastraria malloc/free para un tamano que se conoce en
    // tiempo de compilacion.
    Framebuffer(int w, int h, uint8_t* externalBuf);

    void clear(uint8_t color);
    void setPixel(int x, int y, uint8_t color);
    void fillRect(int x, int y, int w, int h, uint8_t color);
    void shiftRow(int y, int amount);

    const uint8_t* pixels() const { return buf_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_, height_;
    std::vector<uint8_t> owned_;  // vacio si el buffer es externo
    uint8_t* buf_;
};

// Helpers de color, solo para construir la paleta al arrancar. Nada del bucle
// de render los toca: alli ya todo son indices.
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
