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
constexpr int TEX_COUNT = 6;
constexpr int TEX_COLORS = 4;   // colores base por textura
// Seis niveles, no ocho: con seis materiales de pared la rampa se cobra
// TEX_COUNT * TEX_COLORS * SHADE_LEVELS entradas y a ocho niveles la paleta se
// pasaba de los 256 indices que admite el modo 4. El escalonado extra no se
// distingue a 240x160.
constexpr int SHADE_LEVELS = 6;  // niveles de la rampa de sombreado de paredes
constexpr int BG_LEVELS = 16;    // cielo y piso: mas niveles, si no se escalona

struct Texture {
    uint8_t px[TEX_SIZE * TEX_SIZE];  // 0..TEX_COLORS-1
};

namespace detail {

// Paleta de DESIGN.md. Por textura: [0] claro, [1] base, [2] sombra, [3] acento.
// Los tonos derivados salen de escalar el azul base #23314A, nunca de elegir un
// color nuevo a ojo. Por eso varios materiales terminan con el MISMO RGB bit a
// bit (p.ej. el gris de rejilla de GRID, EXIT y DOOR): no es casualidad, es la
// misma paleta de DESIGN.md aplicada a materiales distintos.
constexpr uint8_t BASE_RGB[TEX_COUNT][TEX_COLORS][3] = {
    // PANEL: azul con juntas en rosa fuerte
    {{56, 78, 118}, {35, 49, 74}, {19, 27, 41}, {191, 32, 120}},
    // CONDUIT: azul mas claro, dato en rosa fuerte
    {{74, 98, 140}, {40, 56, 84}, {22, 31, 47}, {191, 32, 120}},
    // GRID: lineas grises sobre azul, cruces en rosa sombra
    {{170, 173, 179}, {31, 43, 66}, {19, 27, 41}, {106, 53, 83}},
    // EXIT: azul muy oscuro, reticula clara y balizas rosas
    {{170, 173, 179}, {26, 37, 58}, {12, 18, 29}, {191, 32, 120}},
    // VAULT: blindaje gris azulado, remaches en rosa sombra
    {{120, 128, 142}, {45, 58, 82}, {24, 33, 50}, {106, 53, 83}},
    // DOOR: la mas oscura, con el cifrado en rosa fuerte
    {{170, 173, 179}, {28, 38, 60}, {14, 20, 32}, {191, 32, 120}},
};

// Deduplica rampas de pared IDENTICAS bit a bit (no aproximadas): dos
// materiales que comparten el mismo RGB en BASE_RGB comparten tambien la
// rampa de sombreado en la paleta final. Se calcula en tiempo de compilacion
// a partir de BASE_RGB, nunca a mano, para que agregar o tocar un material no
// pueda desincronizarse de una tabla de dedup escrita aparte.
struct WallRampTable {
    int count;
    uint8_t rampOf[TEX_COUNT][TEX_COLORS];   // rampa que usa cada (textura, color)
    uint8_t rgb[TEX_COUNT * TEX_COLORS][3];  // color base de cada rampa unica
};

constexpr WallRampTable buildWallRampTable() {
    WallRampTable t{};
    t.count = 0;
    for (int tex = 0; tex < TEX_COUNT; ++tex) {
        for (int c = 0; c < TEX_COLORS; ++c) {
            int found = -1;
            for (int r = 0; r < t.count; ++r) {
                if (t.rgb[r][0] == BASE_RGB[tex][c][0] &&
                    t.rgb[r][1] == BASE_RGB[tex][c][1] &&
                    t.rgb[r][2] == BASE_RGB[tex][c][2]) {
                    found = r;
                    break;
                }
            }
            if (found < 0) {
                found = t.count++;
                t.rgb[found][0] = BASE_RGB[tex][c][0];
                t.rgb[found][1] = BASE_RGB[tex][c][1];
                t.rgb[found][2] = BASE_RGB[tex][c][2];
            }
            t.rampOf[tex][c] = uint8_t(found);
        }
    }
    return t;
}

constexpr WallRampTable WALL_RAMPS = buildWallRampTable();

}  // namespace detail

// Cuantas rampas de pared distintas quedan tras deduplicar. Antes del dedup
// eran TEX_COUNT * TEX_COLORS (24); varios materiales de DESIGN.md comparten
// tono asi que en la practica salen menos.
constexpr int WALL_RAMP_COUNT = detail::WALL_RAMPS.count;

// --- mapa de la paleta --------------------------------------------------------
// Los tramos se calculan, no se escriben a mano: agregar un material de pared
// desplaza todo lo que viene detras y una tabla fija quedaria desincronizada.
//   paredes  WALL_RAMP_COUNT * SHADE_LEVELS  (rampas de color unicas, no
//            tex * color: varios materiales comparten el mismo RGB base -ver
//            detail::buildWallRampTable- y a la GBA le sobran 256 indices
//            para regalar rampas duplicadas)
//   techo / suelo / rejilla        BG_LEVELS cada uno
//   minimapa                       4 colores planos
//   sprites  SPR_COLORS * SHADE_LEVELS
//   interfaz                       5 colores planos
constexpr int PAL_WALLS = 0;
constexpr int PAL_SKY = WALL_RAMP_COUNT * SHADE_LEVELS;
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
constexpr int SPR_COLORS = 5;   // sin contar el transparente
constexpr int ITEM_SPRITES = 5; // RAM, PATCH, CACHE, KEY, PROTOCOLO

constexpr int PAL_SPRITE = PAL_MAP_PLAYER + 1;

// --- interfaz -----------------------------------------------------------------
// Planos, sin rampa de sombreado: el HUD no esta en el mundo, asi que no le
// afecta la distancia.
constexpr int PAL_UI_TEXT = PAL_SPRITE + SPR_COLORS * SHADE_LEVELS;
constexpr int PAL_UI_ACCENT = PAL_UI_TEXT + 1;
constexpr int PAL_UI_DIM = PAL_UI_ACCENT + 1;
constexpr int PAL_UI_BG = PAL_UI_DIM + 1;
constexpr int PAL_UI_WARN = PAL_UI_BG + 1;

constexpr int PALETTE_SIZE = PAL_UI_WARN + 1;

// Primer indice del sprite a un nivel de luz. Se le suma (color - 1) *
// SHADE_LEVELS, igual que en las paredes.
inline uint8_t spriteBase(int level) { return uint8_t(PAL_SPRITE + level); }

// Sprite de mundo. El 0 es transparente y no se dibuja; 1..SPR_COLORS indexan
// la seccion de sprites de la paleta.
struct SpriteFrame {
    uint8_t px[SPR_SIZE * SPR_SIZE];
};

static_assert(PALETTE_SIZE <= 256, "no cabe en la paleta de 8 bits de la GBA");

// Indice final de paleta para un texel: la textura y el color del texel (via
// colorIdx, 0..TEX_COLORS-1) eligen la rampa deduplicada; level elige el
// escalon de sombra dentro de ella.
inline uint8_t wallBase(int texId, int colorIdx, int level) {
    return uint8_t(detail::WALL_RAMPS.rampOf[texId][colorIdx] * SHADE_LEVELS + level);
}

// Rellena una tabla de TEX_COLORS entradas con el indice de paleta final para
// cada color de esa textura a un nivel de sombra fijo. Se llama una vez por
// columna (level es constante en la columna): el bucle interno del renderer
// solo indexa el arreglo con el color del texel, sin multiplicar por pixel.
inline void wallColumnBase(int texId, int level, uint8_t out[TEX_COLORS]) {
    for (int c = 0; c < TEX_COLORS; ++c) {
        out[c] = wallBase(texId, c, level);
    }
}

// Cada caracter del mapa elige un material (ver la tabla de DESIGN.md). El
// generador reparte uno distinto por sala, de modo que dos salas contiguas
// nunca se ven iguales.
inline int texIndex(char impact) {
    switch (impact) {
        case '-': return 1;
        case '|': return 2;
        case 'E': return 3;
        case 'V': return 4;  // carcasa de la camara sellada
        case 'D': return 5;  // puerta cifrada
        default:  return 0;  // '+' y cualquier otra pared
    }
}

// --- clases de sprite ---------------------------------------------------------
// Los enemigos ocupan los indices bajos y los objetos empiezan en SPR_KIND_ITEM.
// Separarlos por rango evita que agregar un enemigo pise el numero de un objeto,
// que es exactamente el error que se cometio al meter el SCOUT.
constexpr int SPR_KIND_WARDEN = 0;
constexpr int SPR_KIND_SCOUT = 1;
constexpr int SPR_KIND_BOSS = 2;
constexpr int SPR_KIND_ITEM = 10;
inline bool spriteIsEnemy(int kind) { return kind < SPR_KIND_ITEM; }

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

// EXIT: marco de extraccion. Las barras diagonales y los nodos rosas forman
// una senal que no se confunde con las tres texturas de salas normales.
inline void fillExit(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            const int dx = x - TEX_SIZE / 2;
            const int dy = y - TEX_SIZE / 2;
            const int adx = dx < 0 ? -dx : dx;
            const int ady = dy < 0 ? -dy : dy;
            uint8_t c = 1;
            if ((adx == 20 || ady == 20) && adx < 21 && ady < 21) c = 3;
            else if ((x + y) % 16 < 2 || (x - y + TEX_SIZE) % 16 < 2) c = 0;
            else if (adx < 8 && ady < 8) c = 2;
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// VAULT: carcasa blindada de la camara sellada. Bloques macizos con remaches,
// sin canales ni datos: se lee como algo cerrado, no como infraestructura.
inline void fillVault(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            const int bx = x & 15, by = y & 15;
            uint8_t c;
            if (bx < 2 || by < 2) c = 2;          // junta hundida
            else if (bx < 4 || by < 4) c = 0;     // bisel
            else if (bx > 5 && bx < 10 && by > 5 && by < 10) c = 3;  // remache
            else c = 1;
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// DOOR: la cerradura. Un rombo de datos girando en el centro sobre bandas
// diagonales, para que desde el pasillo se vea que es algo que se abre y no
// una pared mas de la camara.
inline void fillDoor(Texture& t) {
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            const int dx = x - TEX_SIZE / 2, dy = y - TEX_SIZE / 2;
            const int diamond = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            uint8_t c;
            if (diamond < 8) c = 3;                       // nucleo cifrado
            else if (diamond < 12) c = 0;                 // anillo claro
            else if (((x + y) & 15) < 3) c = 3;           // bandas de aviso
            else if (x < 3 || y < 3 || x > 60 || y > 60) c = 2;
            else c = 1;
            t.px[y * TEX_SIZE + x] = c;
        }
    }
}

// BASE_RGB y la tabla de rampas deduplicadas viven arriba, junto a
// struct Texture: hacen falta antes para calcular WALL_RAMP_COUNT, que el
// mapa de la paleta necesita.

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

inline void fillScout(SpriteFrame& s, int frame) {
    const int core = frame ? 5 : 4;
    for (int y = 0; y < SPR_SIZE; ++y) {
        for (int x = 0; x < SPR_SIZE; ++x) {
            int dx = x - SPR_SIZE / 2, dy = y - SPR_SIZE / 2;
            int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            uint8_t c = 0;
            if (d < core) c = 1;
            else if (d < core + 2) c = 3;
            else if ((x == 5 || x == 26) && y > 9 && y < 23) c = 2;
            s.px[y * SPR_SIZE + x] = c;
        }
    }
}

// Objetos de sistema. Su silueta comunica que se recoge incluso a baja
// resolucion: RAM son modulos, PATCH es una cruz y CACHE es un bloque de datos.
inline void fillRam(SpriteFrame& s) {
    for (int y = 0; y < SPR_SIZE; ++y) for (int x = 0; x < SPR_SIZE; ++x) {
        uint8_t c = 0;
        if (x >= 7 && x <= 24 && y >= 9 && y <= 22) c = 4;
        if (x >= 9 && x <= 22 && y >= 11 && y <= 20) c = 3;
        if (y >= 21 && y <= 25 && (x == 10 || x == 15 || x == 20)) c = 4;
        s.px[y * SPR_SIZE + x] = c;
    }
}

inline void fillPatch(SpriteFrame& s) {
    for (int y = 0; y < SPR_SIZE; ++y) for (int x = 0; x < SPR_SIZE; ++x) {
        const bool vertical = x >= 13 && x <= 18 && y >= 6 && y <= 26;
        const bool horizontal = y >= 13 && y <= 18 && x >= 6 && x <= 26;
        s.px[y * SPR_SIZE + x] = (vertical || horizontal) ? 3 : 0;
    }
}

inline void fillCache(SpriteFrame& s) {
    for (int y = 0; y < SPR_SIZE; ++y) for (int x = 0; x < SPR_SIZE; ++x) {
        uint8_t c = 0;
        if (x >= 7 && x <= 24 && y >= 7 && y <= 24) c = 4;
        if (x >= 9 && x <= 22 && y >= 10 && y <= 21) c = ((x + y) & 4) ? 3 : 4;
        s.px[y * SPR_SIZE + x] = c;
    }
}

// Protocolo de extraccion: anillo de datos con un nucleo rosa. Es mas alto y
// singular que un pickup para leerse como el objetivo de la sala final.
inline void fillProtocol(SpriteFrame& s) {
    for (int y = 0; y < SPR_SIZE; ++y) for (int x = 0; x < SPR_SIZE; ++x) {
        const int dx = x - SPR_SIZE / 2, dy = y - SPR_SIZE / 2;
        const int d2 = dx * dx + dy * dy;
        uint8_t c = 0;
        if (d2 >= 70 && d2 <= 120) c = 4;
        if (d2 < 30) c = 5;
        if ((x == 16 || y == 16) && d2 < 150) c = 3;
        s.px[y * SPR_SIZE + x] = c;
    }
}

// KEY: llave del archivo. Paleton dentado y anilla, la silueta mas reconocible
// que cabe en 32 px; va en rosa fuerte porque es el unico objeto sin el que un
// piso queda incompleto.
inline void fillKey(SpriteFrame& s) {
    for (int y = 0; y < SPR_SIZE; ++y) for (int x = 0; x < SPR_SIZE; ++x) {
        const int dx = x - 10, dy = y - 16;
        const int d2 = dx * dx + dy * dy;
        uint8_t c = 0;
        if (d2 >= 12 && d2 <= 30) c = 5;               // anilla
        if (y >= 14 && y <= 17 && x >= 12 && x <= 25) c = 5;  // caña
        if (x >= 20 && x <= 21 && y >= 17 && y <= 22) c = 5;  // diente largo
        if (x >= 24 && x <= 25 && y >= 17 && y <= 20) c = 5;  // diente corto
        s.px[y * SPR_SIZE + x] = c;
    }
}

// BOSS (NUCLEO CENTINELA): el guardian del ultimo archivo. Misma gramatica que
// el WARDEN -nucleo verde, halo rosa- pero con una coraza hexagonal completa y
// un nucleo que se abre y cierra: se lee como el mismo linaje, mas grande.
inline void fillBoss(SpriteFrame& s, int frame) {
    const int core = frame ? 9 : 6;
    for (int y = 0; y < SPR_SIZE; ++y) {
        for (int x = 0; x < SPR_SIZE; ++x) {
            const int cx = x - SPR_SIZE / 2, cy = y - SPR_SIZE / 2;
            const int ax = cx < 0 ? -cx : cx, ay = cy < 0 ? -cy : cy;
            const int diamond = ax + ay;
            uint8_t c = 0;
            if (diamond < core) c = 1;                       // nucleo
            else if (diamond < core + 3) c = 3;              // halo
            else if (diamond >= 13 && diamond <= 15) c = 2;  // coraza exterior
            else if (diamond >= 16 && diamond <= 18 && ((x + y + frame) & 3) == 0)
                c = 3;                                        // placas girando
            else if (ax <= 1 && ay < 15) c = 2;              // ejes
            else if (ay <= 1 && ax < 15) c = 2;
            s.px[y * SPR_SIZE + x] = c;
        }
    }
}

}  // namespace detail

// Texturas y paleta, construidas una sola vez en el primer uso.
struct Assets {
    Texture tex[TEX_COUNT];
    SpriteFrame warden[SPR_FRAMES];
    SpriteFrame scout[SPR_FRAMES];
    SpriteFrame boss[SPR_FRAMES];
    SpriteFrame item[ITEM_SPRITES];
    uint32_t pal[PALETTE_SIZE];
};

// Corre los generadores de arriba y arma el Assets completo. Es el "codigo
// generador" que la Fase 2 del port hornea a ROM: tools/bake_assets.cpp
// incluye este archivo y llama a esta misma funcion en tiempo de build, asi
// que reusa exactamente esta logica en vez de reescribirla en el host tool.
inline Assets buildAssetsRuntime() {
    Assets a{};
    {
        detail::fillPanel(a.tex[0]);
        detail::fillConduit(a.tex[1]);
        detail::fillGrid(a.tex[2]);
        detail::fillExit(a.tex[3]);
        detail::fillVault(a.tex[4]);
        detail::fillDoor(a.tex[5]);
        for (int f = 0; f < SPR_FRAMES; ++f) detail::fillWarden(a.warden[f], f);
        for (int f = 0; f < SPR_FRAMES; ++f) detail::fillScout(a.scout[f], f);
        for (int f = 0; f < SPR_FRAMES; ++f) detail::fillBoss(a.boss[f], f);
        detail::fillRam(a.item[0]);
        detail::fillPatch(a.item[1]);
        detail::fillCache(a.item[2]);
        detail::fillKey(a.item[3]);
        detail::fillProtocol(a.item[4]);

        // Una rampa por color UNICO, no por (textura, color): el dedup ya
        // paso en WALL_RAMPS, aca solo se sombrea cada rampa una vez.
        for (int r = 0; r < WALL_RAMP_COUNT; ++r) {
            const auto& rgbv = detail::WALL_RAMPS.rgb[r];
            uint32_t base = rgb(rgbv[0], rgbv[1], rgbv[2]);
            for (int l = 0; l < SHADE_LEVELS; ++l) {
                a.pal[r * SHADE_LEVELS + l] = shade(base, float(l + 1) / SHADE_LEVELS);
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

        // Colores de sprite: verde reservado al enemigo; objetos en rosa y
        // gris para que no parezcan una amenaza.
        constexpr uint8_t SPRITE_RGB[SPR_COLORS][3] = {
            {38, 191, 33},   // 1: nucleo
            {24, 110, 21},   // 2: corchetes
            {106, 53, 83},   // 3: halo
            {170, 173, 179}, // 4: carcasa de objetos
            {191, 32, 120},  // 5: datos de objetos
        };
        for (int c = 0; c < SPR_COLORS; ++c) {
            uint32_t base = rgb(SPRITE_RGB[c][0], SPRITE_RGB[c][1], SPRITE_RGB[c][2]);
            for (int l = 0; l < SHADE_LEVELS; ++l) {
                a.pal[PAL_SPRITE + c * SHADE_LEVELS + l] =
                    shade(base, float(l + 1) / SHADE_LEVELS);
            }
        }
        // Interfaz. El verde queda fuera a proposito: es la unica senal de
        // peligro del juego y en una barra de vida dejaria de serlo (regla 1
        // de DESIGN.md).
        a.pal[PAL_UI_TEXT] = rgb(170, 173, 179);   // gris del suelo
        a.pal[PAL_UI_ACCENT] = rgb(191, 32, 120);  // rosa de los detalles
        a.pal[PAL_UI_DIM] = rgb(74, 82, 100);      // gris azulado apagado
        a.pal[PAL_UI_BG] = rgb(12, 16, 26);        // casi negro
        a.pal[PAL_UI_WARN] = rgb(106, 53, 83);     // rosa sombra: integridad baja
    }
    return a;
}

// En GBA (y en desktop, una vez que CMake corrio tools/bake_assets) este
// header generado existe y trae kBakedAssets como datos `const`: en GBA eso
// va a ROM, no a .bss, y no gasta ciclos de arranque generando texturas. Si
// no existe (primer configure, o alguien incluyo Textures.h suelto) se cae al
// camino de siempre, calculado una vez en runtime.
#if __has_include("AssetsData.h")
#include "AssetsData.h"
inline const Assets& assets() { return kBakedAssets; }
#else
inline const Assets& assets() {
    static const Assets a = buildAssetsRuntime();
    return a;
}
#endif

inline const Texture* textures() { return assets().tex; }
inline const SpriteFrame* wardenFrames() { return assets().warden; }
inline const SpriteFrame* scoutFrames() { return assets().scout; }
inline const SpriteFrame* bossFrames() { return assets().boss; }

// Fotograma de un enemigo por su clase de sprite. Centralizado aqui para que
// el renderer no tenga que conocer que clase existe.
inline const SpriteFrame& enemyFrame(int kind, int frame) {
    const int f = frame & (SPR_FRAMES - 1);
    switch (kind) {
        case SPR_KIND_SCOUT: return scoutFrames()[f];
        case SPR_KIND_BOSS:  return bossFrames()[f];
        default:             return wardenFrames()[f];
    }
}
inline const SpriteFrame& itemFrame(int kind) {
    return assets().item[(kind - SPR_KIND_ITEM) % ITEM_SPRITES];
}
inline const uint32_t* palette() { return assets().pal; }
