#pragma once

#include <cstdint>

#include "Framebuffer.h"  // rgb()

// Texturas generadas por codigo en vez de cargadas de disco: no hace falta
// ninguna libreria de imagenes ni archivos de assets, y para el port a GBA
// esto mismo se hornea a paletas de 8 bits en tiempo de compilacion.

constexpr int TEX_SIZE = 64;  // potencia de 2: permite enmascarar en vez de %
constexpr int TEX_COUNT = 3;

struct Texture {
    uint32_t px[TEX_SIZE * TEX_SIZE];
};

// Cada caracter del laberinto elige una textura. Conserva la distincion de
// colores que tenia wall_color() en la version Rust.
inline int texIndex(char impact) {
    switch (impact) {
        case '-': return 1;
        case '|': return 2;
        default:  return 0;  // '+' y cualquier otra pared
    }
}

namespace detail {

// Ladrillos: hiladas de 16 px de alto y 32 de ancho, desfasadas una fila si y
// otra no, con 2 px de mortero.
inline void fillBricks(Texture& t, uint32_t brick, uint32_t dark, uint32_t mortar) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        int row = y / 16;
        int offset = (row & 1) ? 16 : 0;
        for (int x = 0; x < TEX_SIZE; ++x) {
            int bx = (x + offset) % 32;
            bool isMortar = (y % 16) < 2 || bx < 2;
            // franja superior del ladrillo mas clara: da relieve sin normales
            uint32_t c = isMortar ? mortar : (((y % 16) < 5) ? brick : dark);
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

}  // namespace detail

// Las tres texturas, construidas una sola vez en el primer uso.
inline const Texture* textures() {
    static Texture tex[TEX_COUNT];
    static bool built = false;
    if (!built) {
        detail::fillBricks(tex[0], rgb(102, 191, 255), rgb(72, 145, 200), rgb(35, 60, 85));
        detail::fillBricks(tex[1], rgb(0, 121, 241), rgb(0, 92, 185), rgb(10, 30, 60));
        detail::fillBricks(tex[2], rgb(0, 82, 172), rgb(0, 60, 130), rgb(8, 22, 45));
        built = true;
    }
    return tex;
}
