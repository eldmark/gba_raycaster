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
// [96..111]  rampa de techo
// [112..127] rampa de suelo
// [128..143] rampa de la linea de rejilla del suelo
// [144..147] colores planos del minimapa
// [148..171] colores de sprites (3 x 8 niveles)
constexpr int PAL_WALLS = 0;
constexpr int PAL_SKY = TEX_COUNT * TEX_COLORS * SHADE_LEVELS;
constexpr int PAL_FLOOR = PAL_SKY + BG_LEVELS;
constexpr int PAL_FLOOR_LINE = PAL_FLOOR + BG_LEVELS;
constexpr int PAL_MAP_BG = PAL_FLOOR_LINE + BG_LEVELS;
constexpr int PAL_MAP_WALL = PAL_MAP_BG + 1;
constexpr int PAL_MAP_RAY = PAL_MAP_WALL + 1;
constexpr int PAL_MAP_PLAYER = PAL_MAP_RAY + 1;

// --- sprites ------------------------------------------------------------------
// El indice 0 de un sprite es transparente, asi que solo los colores 1..N
// ocupan paleta.
constexpr int SPR_SIZE = 32;    // potencia de 2, como las paredes
constexpr int SPR_FRAMES = 2;   // animacion de latido del guardian
constexpr int SPR_COLORS = 3;   // sin contar el transparente

constexpr int PAL_SPRITE = PAL_MAP_PLAYER + 1;
constexpr int PALETTE_SIZE = PAL_SPRITE + SPR_COLORS * SHADE_LEVELS;

// Primer indice del sprite a un nivel de luz. Se le suma (color - 1) *
// SHADE_LEVELS, igual que en las paredes.
inline uint8_t spriteBase(int level) { return uint8_t(PAL_SPRITE + level); }

// Sprite de mundo. El 0 es transparente y no se dibuja; 1..SPR_COLORS indexan
// la seccion de sprites de la paleta.
struct SpriteFrame {
    uint8_t px[SPR_SIZE * SPR_SIZE];
};

static_assert(PALETTE_SIZE <= 256, "no cabe en la paleta de 8 bits de la GBA");

// Primer indice de una textura a un nivel de luz dado. Sumar
// colorIdx * SHADE_LEVELS da el indice final del texel.
inline uint8_t wallBase(int texId, int level) {
    return uint8_t((texId * TEX_COLORS) * SHADE_LEVELS + level);
}

// Cada caracter del mapa elige un material (ver la tabla de DESIGN.md). El
// generador reparte uno distinto por sala, de modo que dos salas contiguas
// nunca se ven iguales.
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

// PANEL: placas de 32x32 con junta rosa y un nodo en cada cruce. La superficie
// se deja casi lisa a proposito: el detalle vive en las juntas, no en el relleno.
inline void fillPanel(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int bx = x & 31, by = y & 31;
            uint8_t c;
            if (bx == 0 || by == 0) {
                c = 3;  // junta rosa
            } else if (bx < 3 && by < 3) {
                c = 3;  // nodo de la esquina
            } else if (bx < 2 || by < 2) {
                c = 0;  // bisel iluminado junto a la junta
            } else {
                // banda mas oscura en la mitad baja: da un arriba y un abajo
                c = (by > 20) ? 2 : 1;
            }
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// CONDUIT: conductos verticales cada 16 px con datos corriendo dentro. Los
// tramos rosa son de largo desigual, si no se lee como una cremallera.
inline void fillConduit(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int bx = x & 15;
            uint8_t c;
            if (bx == 0) {
                c = 2;  // separacion entre conductos
            } else if (bx >= 6 && bx <= 9) {
                // el canal del dato: encendido a tramos
                c = ((y + (x >> 4) * 5) & 15) < 9 ? 3 : 2;
            } else if (bx == 5 || bx == 10) {
                c = 0;  // borde brillante del canal
            } else {
                c = 1;
            }
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// GRID: rejilla fina gris sobre el azul base, con los cruces marcados. Es el
// material mas "vacio" de los tres, para que las salas que lo usan se lean
// como espacio muerto.
inline void fillGrid(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            bool lineX = (x & 7) == 0;
            bool lineY = (y & 7) == 0;
            uint8_t c;
            if (lineX && lineY) {
                c = 3;  // cruce
            } else if (lineX || lineY) {
                c = 0;  // linea gris
            } else {
                c = ((x & 7) < 4) == ((y & 7) < 4) ? 1 : 2;  // damero muy sutil
            }
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// Paleta de DESIGN.md. Por textura: [0] claro, [1] base, [2] sombra, [3] acento.
// Los tonos derivados salen de escalar el azul base #23314A, nunca de elegir un
// color nuevo a ojo.
constexpr uint8_t BASE_RGB[TEX_COUNT][TEX_COLORS][3] = {
    // PANEL: azul con juntas en rosa fuerte
    {{56, 78, 118}, {35, 49, 74}, {19, 27, 41}, {191, 32, 120}},
    // CONDUIT: azul mas claro, dato en rosa fuerte
    {{74, 98, 140}, {40, 56, 84}, {22, 31, 47}, {191, 32, 120}},
    // GRID: lineas grises sobre azul, cruces en rosa sombra
    {{170, 173, 179}, {31, 43, 66}, {19, 27, 41}, {106, 53, 83}},
};

// WARDEN: proceso guardian. Un nucleo en rombo que late entre los dos
// fotogramas, con cuatro corchetes fijos alrededor. Verde brillante solo en el
// nucleo, que es el unico sitio del juego donde ese verde aparece (regla 1 de
// DESIGN.md); el resto va en rosa sombra para que no compita.
inline void fillWarden(SpriteFrame& s, int frame) {
    const int core = frame ? 7 : 5;  // el latido
    for (int y = 0; y < SPR_SIZE; ++y) {
        for (int x = 0; x < SPR_SIZE; ++x) {
            int cx = x - SPR_SIZE / 2;
            int cy = y - SPR_SIZE / 2;
            int diamond = (cx < 0 ? -cx : cx) + (cy < 0 ? -cy : cy);
            int ax = cx < 0 ? -cx : cx;
            int ay = cy < 0 ? -cy : cy;

            uint8_t c = 0;  // transparente
            if (diamond < core) {
                c = 1;  // nucleo verde
            } else if (diamond < core + 3) {
                c = 3;  // halo rosa sombra
            } else if (ax >= 10 && ay >= 10 && ax <= 14 && ay <= 14 &&
                       (ax >= 13 || ay >= 13)) {
                c = 2;  // corchetes de las esquinas, verde apagado
            }
            s.px[y * SPR_SIZE + x] = c;
        }
    }
}

}  // namespace detail

// Texturas y paleta, construidas una sola vez en el primer uso.
struct Assets {
    Texture tex[TEX_COUNT];
    SpriteFrame warden[SPR_FRAMES];
    uint32_t pal[PALETTE_SIZE];
};

inline const Assets& assets() {
    static Assets a;
    static bool built = false;
    if (!built) {
        detail::fillPanel(a.tex[0]);
        detail::fillConduit(a.tex[1]);
        detail::fillGrid(a.tex[2]);
        for (int f = 0; f < SPR_FRAMES; ++f) detail::fillWarden(a.warden[f], f);

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

        // Techo: casi negro arriba, azul base cerca del horizonte. No lleva
        // detalle a proposito (regla 4 de DESIGN.md).
        // Suelo: gris, oscuro en el horizonte (lejos) y claro a los pies.
        for (int l = 0; l < BG_LEVELS; ++l) {
            float f = float(l + 1) / BG_LEVELS;
            a.pal[PAL_SKY + l] = shade(rgb(35, 49, 74), 0.12f + 0.55f * f);
            a.pal[PAL_FLOOR + l] = shade(rgb(170, 173, 179), 0.18f + 0.52f * f);
            // linea de la rejilla: el mismo gris del suelo pero mas claro, no
            // rosa. El rosa es acento de pared y en el suelo, repetido doce
            // veces, deja de ser acento (regla 2 de DESIGN.md).
            a.pal[PAL_FLOOR_LINE + l] =
                shade(rgb(170, 173, 179), 0.30f + 0.70f * f);
        }

        a.pal[PAL_MAP_BG] = rgb(10, 14, 22);
        a.pal[PAL_MAP_WALL] = rgb(35, 49, 74);
        a.pal[PAL_MAP_RAY] = rgb(191, 32, 120);
        a.pal[PAL_MAP_PLAYER] = rgb(170, 173, 179);

        // Colores de sprite: verde de enemigo, su version apagada, y el rosa
        // sombra para el detalle.
        constexpr uint8_t SPRITE_RGB[SPR_COLORS][3] = {
            {38, 191, 33},   // 1: nucleo
            {24, 110, 21},   // 2: corchetes
            {106, 53, 83},   // 3: halo
        };
        for (int c = 0; c < SPR_COLORS; ++c) {
            uint32_t base = rgb(SPRITE_RGB[c][0], SPRITE_RGB[c][1], SPRITE_RGB[c][2]);
            for (int l = 0; l < SHADE_LEVELS; ++l) {
                a.pal[PAL_SPRITE + c * SHADE_LEVELS + l] =
                    shade(base, float(l + 1) / SHADE_LEVELS);
            }
        }
        built = true;
    }
    return a;
}

inline const Texture* textures() { return assets().tex; }
inline const SpriteFrame* wardenFrames() { return assets().warden; }
inline const uint32_t* palette() { return assets().pal; }
