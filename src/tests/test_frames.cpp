// Frames dorados: la red que sostiene el port a GBA.
//
// Las fases de optimizacion del port reescriben el bucle interno del
// renderizador. Un test que solo diga "sigue compilando" no sirve ahi: hace
// falta poder demostrar que la IMAGEN no cambia, y cuando cambia a proposito,
// cuanto cambia.
//
// Por eso este test no compara un booleano sino un RECUENTO DE PIXELES. La
// diferencia entre los dos numeros es toda la informacion que importa:
//
//   reemplazar fxDiv por un reciproco de 1 ulp  ->    149 pixeles de 921.600
//   recortar mal la razon de un sprite          ->  miles, y sprites movidos
//
// El segundo caso es real: se cometio al arreglar el desbordamiento de screenX
// y fue esta comparacion la que lo detecto, no la suite de siempre.
//
// Uso:
//   test_frames              compara contra la referencia
//   test_frames --bless      regenera la referencia (a proposito, y se commitea)

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Framebuffer.h"
#include "Game.h"
#include "Hud.h"
#include "Renderer.h"
#include "Textures.h"

namespace {

// Resolucion de la consola, que es la que hay que proteger: lo que cabe en
// 240x160 cabe en cualquier ventana, y al reves no.
constexpr int W = 240;
constexpr int H = 160;
constexpr int FRAME_BYTES = W * H;

constexpr fx DT = FX_ONE / 60;

// Semillas y pisos elegidos para cubrir materiales distintos, camara sellada,
// enemigos a media distancia y el ultimo archivo con el jefe.
const uint32_t SEEDS[] = {42u, 583291u, 7919u};
constexpr int FLOORS = 2;
constexpr int TURNS = 4;
constexpr int FRAME_COUNT = 3 * FLOORS * TURNS;

// Renderiza la escena completa igual que el bucle de escritorio: mundo,
// sprites, minimapa y HUD. Si alguna de esas capas se rompe, se ve aqui.
void renderFrame(Game& game, Framebuffer& fb) {
    renderWorld(fb, game.maze(), game.player());

    SpriteInstance sprites[Game::MAX_WORLD_SPRITES];
    int n = game.buildSprites(sprites, Game::MAX_ENEMIES);
    n += game.buildItemSprites(sprites + n, Game::MAX_WORLD_SPRITES - n);
    renderSprites(fb, game.player(), sprites, n);

    renderMinimap(fb, game.maze(), game.player());
    renderMinimapEntities(fb, game.maze(), game.player(), sprites, n);
    drawHud(fb, game);
}

// Genera los FRAME_COUNT frames en un solo buffer contiguo. Determinista: la
// misma seed produce el mismo mapa, los mismos enemigos y el mismo loot, asi
// que dos ejecuciones solo pueden diferir si cambio el codigo.
void captureAll(std::vector<uint8_t>& out) {
    out.resize(size_t(FRAME_COUNT) * FRAME_BYTES);
    size_t offset = 0;

    for (uint32_t seed : SEEDS) {
        Game game;
        game.newRun(seed);
        for (int floor = 0; floor < FLOORS; ++floor) {
            for (int turn = 0; turn < TURNS; ++turn) {
                // Girar entre capturas da cuatro vistas distintas del mismo
                // piso sin tener que caminar hasta la salida.
                Input in;
                in.right = true;
                for (int k = 0; k < 12; ++k) game.update(in, DT);

                Framebuffer fb(W, H);
                renderFrame(game, fb);
                std::memcpy(out.data() + offset, fb.pixels(), FRAME_BYTES);
                offset += FRAME_BYTES;
            }
        }
    }
}

const char* refPath() { return FRAMES_REF; }

bool writeRef(const std::vector<uint8_t>& frames) {
    FILE* f = std::fopen(refPath(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(frames.data(), 1, frames.size(), f) == frames.size();
    std::fclose(f);
    return ok;
}

bool readRef(std::vector<uint8_t>& ref) {
    FILE* f = std::fopen(refPath(), "rb");
    if (!f) return false;
    ref.resize(size_t(FRAME_COUNT) * FRAME_BYTES);
    const size_t got = std::fread(ref.data(), 1, ref.size(), f);
    std::fclose(f);
    return got == ref.size();
}

}  // namespace

int main(int argc, char** argv) {
    const bool bless = argc > 1 && std::strcmp(argv[1], "--bless") == 0;

    std::vector<uint8_t> frames;
    captureAll(frames);

    if (bless) {
        if (!writeRef(frames)) {
            std::fprintf(stderr, "no se pudo escribir %s\n", refPath());
            return 1;
        }
        std::printf("referencia regenerada: %d frames, %zu bytes -> %s\n",
                    FRAME_COUNT, frames.size(), refPath());
        return 0;
    }

    std::vector<uint8_t> ref;
    if (!readRef(ref)) {
        std::fprintf(stderr,
                     "falta la referencia (%s).\n"
                     "Generala con: ./test_frames --bless\n",
                     refPath());
        return 1;
    }

    // Recuento por frame, no un simple distinto/igual: cuantos pixeles se
    // movieron es lo que separa un redondeo de un bug.
    int framesChanged = 0;
    long totalDiff = 0;
    int worstFrame = -1;
    long worstDiff = 0;

    for (int i = 0; i < FRAME_COUNT; ++i) {
        const uint8_t* a = ref.data() + size_t(i) * FRAME_BYTES;
        const uint8_t* b = frames.data() + size_t(i) * FRAME_BYTES;
        long diff = 0;
        for (int p = 0; p < FRAME_BYTES; ++p) {
            if (a[p] != b[p]) ++diff;
        }
        if (diff > 0) {
            ++framesChanged;
            totalDiff += diff;
            if (diff > worstDiff) {
                worstDiff = diff;
                worstFrame = i;
            }
        }
    }

    if (totalDiff != 0) {
        const long totalPixels = long(FRAME_COUNT) * FRAME_BYTES;
        std::fprintf(stderr,
                     "\nLA IMAGEN CAMBIO\n"
                     "  frames afectados : %d de %d\n"
                     "  pixeles distintos: %ld de %ld (%.4f%%)\n"
                     "  peor frame       : #%d con %ld pixeles (%.3f%% de la pantalla)\n"
                     "\n"
                     "Unas pocas decenas de pixeles en bordes de pared es un cambio\n"
                     "de redondeo esperable. Miles, o sprites desplazados, es un bug.\n"
                     "Si el cambio es intencionado: ./test_frames --bless\n\n",
                     framesChanged, FRAME_COUNT, totalDiff, totalPixels,
                     100.0 * double(totalDiff) / double(totalPixels), worstFrame,
                     worstDiff, 100.0 * double(worstDiff) / FRAME_BYTES);
    }
    assert(totalDiff == 0);

    std::printf("all tests passed (%d frames identicos)\n", FRAME_COUNT);
    return 0;
}
