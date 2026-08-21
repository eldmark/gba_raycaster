// Prueba de equivalencia RGB para el dedup de rampas de pared.
//
// El dedup en Textures.h fusiona rampas cuyo RGB base es IDENTICO bit a bit
// (ver detail::buildWallRampTable). Este programa no confia en esa
// afirmacion: recalcula el color de cada (textura, color, nivel) desde
// BASE_RGB de forma independiente -sin pasar por WALL_RAMPS ni por
// wallBase()- y lo compara contra el color que de verdad termina en
// assets().pal[] tras el dedup. Si el dedup fuera "aproximado" (fusionar
// colores parecidos pero no iguales) esta comparacion lo detectaria con un
// error de canal distinto de cero.
//
// No es un test dorado de framebuffer (eso es test_frames, con sus propios
// indices, que SI cambian con el dedup y eso es lo esperado). Esto compara
// RGB contra RGB, que es lo que de verdad importa: que se vea igual.
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <algorithm>

#include "Textures.h"

int main() {
    const Assets& a = assets();

    int maxErr[3] = {0, 0, 0};
    long compared = 0;

    for (int t = 0; t < TEX_COUNT; ++t) {
        for (int c = 0; c < TEX_COLORS; ++c) {
            const uint32_t groundBase =
                rgb(detail::BASE_RGB[t][c][0], detail::BASE_RGB[t][c][1],
                    detail::BASE_RGB[t][c][2]);
            for (int l = 0; l < SHADE_LEVELS; ++l) {
                const uint32_t expected = shade(groundBase, float(l + 1) / SHADE_LEVELS);
                const uint32_t actual = a.pal[wallBase(t, c, l)];

                const int er = int((expected >> 16) & 0xFF) - int((actual >> 16) & 0xFF);
                const int eg = int((expected >> 8) & 0xFF) - int((actual >> 8) & 0xFF);
                const int eb = int(expected & 0xFF) - int(actual & 0xFF);
                if (er || eg || eb) {
                    fprintf(stderr,
                        "MISMATCH tex=%d color=%d level=%d: esperado=%08X actual=%08X\n",
                        t, c, l, expected, actual);
                }
                assert(er == 0 && eg == 0 && eb == 0);
                if (er < 0) maxErr[0] = std::max(maxErr[0], -er); else maxErr[0] = std::max(maxErr[0], er);
                if (eg < 0) maxErr[1] = std::max(maxErr[1], -eg); else maxErr[1] = std::max(maxErr[1], eg);
                if (eb < 0) maxErr[2] = std::max(maxErr[2], -eb); else maxErr[2] = std::max(maxErr[2], eb);
                ++compared;
            }
        }
    }

    const int oldSize = TEX_COUNT * TEX_COLORS * SHADE_LEVELS;
    const int newSize = WALL_RAMP_COUNT * SHADE_LEVELS;

    printf("Comparados %ld texels (textura,color,nivel) de pared.\n", compared);
    printf("Error maximo por canal (R,G,B) tras el dedup: %d,%d,%d\n",
           maxErr[0], maxErr[1], maxErr[2]);
    printf("Rampas de pared: %d (antes del dedup) -> %d (despues)\n",
           TEX_COUNT * TEX_COLORS, WALL_RAMP_COUNT);
    printf("Entradas de paleta solo-pared: %d -> %d\n", oldSize, newSize);
    printf("PALETTE_SIZE total: %d\n", PALETTE_SIZE);

    if (maxErr[0] != 0 || maxErr[1] != 0 || maxErr[2] != 0) {
        fprintf(stderr, "FALLO: el dedup cambio color (deberia ser exacto)\n");
        return 1;
    }
    printf("OK: RGB identico antes y despues del dedup.\n");
    return 0;
}
