#pragma once

#include <cstdint>

#include "Framebuffer.h"  // rgb(), shade()

// Texturas y paleta, con el formato que espera el modo 4 de la GBA: cada texel
// es un indice de 8 bits y el color real vive en una paleta unica.
//
// El sombreado por distancia esta HORNEADO en la paleta: cada color base
// aparece SHADE_LEVELS veces, de oscuro a claro. Asi el bucle interno del
// renderer no multiplica floats por pixel, solo suma un entero al indice.
// En GBA esa paleta se copia tal cual a BG_PALETTE y el sombreado sale gratis.

constexpr int TEX_SIZE = 64;    // potencia de 2: permite enmascarar en vez de %
constexpr int TEX_COUNT = 3;
constexpr int TEX_COLORS = 4;   // colores base por textura
constexpr int SHADE_LEVELS = 8;  // niveles de la rampa de sombreado de paredes
constexpr int BG_LEVELS = 16;    // cielo y piso: mas niveles, si no se escalona

struct Texture {
    uint8_t px[TEX_SIZE * TEX_SIZE];  // 0..TEX_COLORS-1
};

// --- mapa de la paleta --------------------------------------------------------
// [0..95]    paredes: (tex * TEX_COLORS + color) * SHADE_LEVELS + nivel
// [96..111]  rampa de cielo
// [112..127] rampa de piso
// [128..131] colores planos del minimapa
constexpr int PAL_WALLS = 0;
constexpr int PAL_SKY = TEX_COUNT * TEX_COLORS * SHADE_LEVELS;
constexpr int PAL_FLOOR = PAL_SKY + BG_LEVELS;
constexpr int PAL_MAP_BG = PAL_FLOOR + BG_LEVELS;
constexpr int PAL_MAP_WALL = PAL_MAP_BG + 1;
constexpr int PAL_MAP_RAY = PAL_MAP_WALL + 1;
constexpr int PAL_MAP_PLAYER = PAL_MAP_RAY + 1;
constexpr int PALETTE_SIZE = PAL_MAP_PLAYER + 1;

static_assert(PALETTE_SIZE <= 256, "no cabe en la paleta de 8 bits de la GBA");

// Primer indice de una textura a un nivel de luz dado. Sumar
// colorIdx * SHADE_LEVELS da el indice final del texel.
inline uint8_t wallBase(int texId, int level) {
    return uint8_t((texId * TEX_COLORS) * SHADE_LEVELS + level);
}

// Cada caracter del laberinto elige una textura. Conserva la distincion que
// tenia wall_color() en la version Rust.
inline int texIndex(char impact) {
    switch (impact) {
        case '-': return 1;
        case '|': return 2;
        default:  return 0;  // '+' y cualquier otra pared
    }
}

namespace detail {

// Hash entero deterministico: da moteado sin tabla de ruido ni rand().
inline uint8_t noise(int x, int y) {
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return uint8_t(h);
}

// Sillares de 32x32 con junta de 2 px, hiladas alternas desfasadas.
inline void fillStone(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        int off = ((y >> 5) & 1) ? 16 : 0;
        for (int x = 0; x < TEX_SIZE; ++x) {
            int bx = (x + off) & 31;
            uint8_t c;
            if (bx < 2 || (y & 31) < 2) {
                c = 3;  // junta
            } else {
                // ruido a media resolucion: a pixel completo queda como nieve
                uint8_t n = noise(x >> 1, y >> 1);
                c = (n < 48) ? 2 : (n < 160 ? 1 : 0);
            }
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// Ladrillos de 32x16 con mortero de 2 px y franja superior clara: da relieve
// sin necesidad de normales ni luz real.
inline void fillBricks(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        int off = ((y >> 4) & 1) ? 16 : 0;
        for (int x = 0; x < TEX_SIZE; ++x) {
            int bx = (x + off) & 31;
            int by = y & 15;
            uint8_t c;
            if (by < 2 || bx < 2) {
                c = 3;  // mortero
            } else if (by < 4) {
                c = 0;  // canto iluminado del ladrillo
            } else {
                c = (noise(x >> 1, y >> 1) < 70) ? 2 : 1;
            }
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// Paneles metalicos de 16x32 con bisel lateral y un remache al centro.
inline void fillPanels(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int px = x & 15;
            int py = y & 31;
            uint8_t c;
            if (px < 1 || py < 1) {
                c = 3;  // costura
            } else {
                int dx = px - 8;
                int dy = (py & 15) - 8;
                if (dx * dx + dy * dy <= 4) {
                    c = (dy < 0) ? 0 : 2;  // remache: brillo arriba, sombra abajo
                } else {
                    c = (px < 3) ? 0 : (px > 12 ? 2 : 1);  // bisel del panel
                }
            }
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// Colores base: [0] claro, [1] medio, [2] oscuro, [3] junta.
constexpr uint8_t BASE_RGB[TEX_COUNT][TEX_COLORS][3] = {
    {{150, 160, 175}, {110, 120, 138}, {78, 86, 102}, {48, 54, 66}},   // piedra
    {{178, 92, 66},   {146, 70, 50},   {112, 52, 38}, {92, 88, 80}},   // ladrillo
    {{120, 168, 214}, {74, 116, 160},  {46, 76, 110}, {26, 42, 64}},   // metal
};

}  // namespace detail

// Texturas y paleta, construidas una sola vez en el primer uso.
struct Assets {
    Texture tex[TEX_COUNT];
    uint32_t pal[PALETTE_SIZE];
};

inline const Assets& assets() {
    static Assets a;
    static bool built = false;
    if (!built) {
        detail::fillStone(a.tex[0]);
        detail::fillBricks(a.tex[1]);
        detail::fillPanels(a.tex[2]);

        for (int t = 0; t < TEX_COUNT; ++t) {
            for (int c = 0; c < TEX_COLORS; ++c) {
                const auto& rgbv = detail::BASE_RGB[t][c];
                uint32_t base = rgb(rgbv[0], rgbv[1], rgbv[2]);
                for (int l = 0; l < SHADE_LEVELS; ++l) {
                    a.pal[wallBase(t, l) + c * SHADE_LEVELS] =
                        shade(base, float(l + 1) / SHADE_LEVELS);
                }
            }
        }

        // cielo: oscuro arriba, mas claro hacia el horizonte.
        // piso: oscuro en el horizonte (lejos), mas claro a los pies (cerca).
        for (int l = 0; l < BG_LEVELS; ++l) {
            float f = float(l + 1) / BG_LEVELS;
            a.pal[PAL_SKY + l] = shade(rgb(70, 96, 150), 0.35f + 0.65f * f);
            a.pal[PAL_FLOOR + l] = shade(rgb(96, 84, 68), 0.35f + 0.65f * f);
        }

        a.pal[PAL_MAP_BG] = rgb(0, 0, 0);
        a.pal[PAL_MAP_WALL] = rgb(102, 191, 255);
        a.pal[PAL_MAP_RAY] = rgb(230, 41, 55);
        a.pal[PAL_MAP_PLAYER] = rgb(253, 249, 0);
        built = true;
    }
    return a;
}

inline const Texture* textures() { return assets().tex; }
inline const uint32_t* palette() { return assets().pal; }
