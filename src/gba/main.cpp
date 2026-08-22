#include "Debug.h"
#include "Fixed.h"
#include "Framebuffer.h"
#include "Game.h"
#include "Hud.h"
#include "Platform.h"
#include "Renderer.h"
#include "Text.h"
#include "Textures.h"

#include <cstdio>

constexpr int WIDTH = 240;
constexpr int HEIGHT = 160;

// El framebuffer del juego vive en EWRAM, no en VRAM: el modo 4 rechaza
// escrituras de 8 bits (strb duplica el byte en las dos mitades del
// halfword), y setPixel/fillRect/shiftRow del motor escriben byte a byte. La
// salida correcta para esta fase es renderizar aqui y que Platform::present
// vuelque a VRAM con escrituras de 16 bits emparejadas; hacerlo mas barato
// (DMA, pares nativos en el propio bucle de render) es la fase 4.
// alignas(4): present() lo vuelca con DMA de 32 bits, y el DMA ignora los dos
// bits bajos de la direccion. Un buffer desalineado se copiaria corrido.
alignas(4) static uint8_t g_fbBuf[WIDTH * HEIGHT];

// Todo lo que sigue -- contadores, overlay en pantalla y volcado al log de
// mGBA -- es el instrumental de medida de la fase 3, y solo entra en la ROM
// del target "profile" (-DGBA_PROFILE). El cartucho que se entrega no lleva
// nada de esto: la medida pintaba dos numeros encima de cada pantalla.
#ifdef GBA_PROFILE

// Cuantos frames medir al arrancar antes de dejar de imprimir. La pantalla de
// titulo y una partida ya en curso cuestan distinto (mas o menos sprites,
// HUD, minimapa), asi que se registran los dos por separado.
#ifdef GBA_AUTOSTART
constexpr int kProfileFrames = 2;
#else
constexpr int kProfileFrames = 90;
#endif

// Globales sin decorar (extern "C", nombre plano) para poder leerlas por
// nombre desde fuera con arm-none-eabi-gdb via el gdbserver de mgba (-g):
// el log de depuracion de mGBA (0x4FFF600) resulto no imprimir nada bajo el
// frontend SDL en este entorno, asi que la medida real se lee de memoria en
// vez de confiar en ese canal.
extern "C" volatile uint32_t g_titleCycles;
volatile uint32_t g_titleCycles = 0;
extern "C" volatile uint32_t g_playingCycles;
volatile uint32_t g_playingCycles = 0;
extern "C" volatile uint32_t g_presentCycles;
volatile uint32_t g_presentCycles = 0;   // solo el volcado EWRAM->VRAM + vblank

#endif  // GBA_PROFILE

// Cada etapa del frame por separado. Sin esto solo se sabe que un frame cuesta
// 6.460.017 ciclos, que no dice cual de las seis etapas hay que arreglar. Fuera
// del target "profile" la macro se evapora y queda la llamada pelada.
#ifdef GBA_PROFILE
#define PROF_DECL(name) \
    extern "C" volatile uint32_t g_##name##Cycles; \
    volatile uint32_t g_##name##Cycles = 0
PROF_DECL(world);
PROF_DECL(sprites);
PROF_DECL(minimap);
PROF_DECL(hud);
PROF_DECL(screen);
// Los cuenta Platform.cpp, que es quien puede separar el volcado a VRAM de la
// espera al vblank: uno se puede optimizar y el otro no.
extern "C" volatile uint32_t g_copyCycles;
extern "C" volatile uint32_t g_vblankCycles;
// Los cuenta Renderer.cpp: fondo, DDA y bucle de texturas por separado.
extern "C" volatile uint32_t g_bgCycles;
extern "C" volatile uint32_t g_rayCycles;
extern "C" volatile uint32_t g_texCycles;
#define PROF_MARK(name, ...)                              \
    do {                                                  \
        const uint32_t profStart = gbadbg::cycles();      \
        __VA_ARGS__;                                      \
        g_##name##Cycles = gbadbg::cycles() - profStart;  \
    } while (0)
#else
#define PROF_MARK(name, ...) \
    do {                     \
        __VA_ARGS__;         \
    } while (0)
#endif

int main() {
    Platform platform;
    platform.init(WIDTH, HEIGHT, "VIOLET HAT");

    Framebuffer fb(WIDTH, HEIGHT, g_fbBuf);

    // static, y no una local: Game mide unos 22 KB -- Nav son 16.384 bytes de
    // campos de navegacion y cola, y Maze otros 4.096-- y la pila entera de la
    // consola son 32 KB de IWRAM que ademas comparte sitio con el codigo que
    // se coloca ahi. Como local dejaba el puntero de pila en 0x03002678,
    // por debajo del final del codigo en IWRAM, machacandolo. Siendo static
    // vive en .bss, que este linker script manda a EWRAM (256 KB), y la pila
    // vuelve a tener los 32 KB para lo que son.
    static Game game;
    // No hay RTC accesible sin hardware extra ni argv en un cartucho: semilla
    // fija. Reproducible es mejor que aleatorio de verdad para poder comparar
    // mediciones entre corridas.
    game.setSeed(12345u);

    Input input;
    fx dt = FX_ONE / 60;

#if defined(GBA_PROFILE) || defined(GBA_AUTOSTART)
    int frame = 0;
#endif
#ifdef GBA_PROFILE
    int playingFrames = 0;  // frames consecutivos en State::Playing
    bool loggedTitle = false;
    bool loggedPlaying = false;
#endif

    for (;;) {
        platform.pollInput(input);

#ifdef GBA_AUTOSTART
        // ponytail: sin xdotool/lua en este entorno no hay forma de inyectar
        // botones en mgba-sdl headless. Este pulso sintetico de START solo
        // existe en el target "profile" del Makefile (GBA_AUTOSTART) para
        // poder medir el frame de juego sin manos; el .gba normal no lo
        // lleva y responde solo al KEYPAD real.
        if (frame == 12 || frame == 15) input.start = true;
#endif

#ifdef GBA_PROFILE
        const uint32_t before = gbadbg::cycles();
#endif

        game.update(input, dt);

        if (game.state() == Game::State::Playing) {
            PROF_MARK(world, renderWorld(fb, game.maze(), game.player()));

            SpriteInstance sprites[Game::MAX_WORLD_SPRITES];
            int spriteCount = game.buildSprites(sprites, Game::MAX_ENEMIES);
            spriteCount += game.buildItemSprites(sprites + spriteCount,
                                                 Game::MAX_WORLD_SPRITES - spriteCount);
            PROF_MARK(sprites, renderSprites(fb, game.player(), sprites, spriteCount));

            PROF_MARK(minimap, {
                renderMinimap(fb, game.maze(), game.player());
                renderMinimapEntities(fb, game.maze(), game.player(), sprites, spriteCount);
            });
            PROF_MARK(hud, {
                drawHud(fb, game);
                renderConnectionLoss(fb, game.connectionLoss(), game.glitchPhase());
            });
        } else {
            PROF_MARK(screen, drawScreen(fb, game));
        }

#ifdef GBA_PROFILE
        // Overlay con la ultima medida disponible (la del frame anterior: la
        // de este frame todavia no se conoce porque incluye el propio
        // present()). Se dibuja SIEMPRE, sobre cualquier pantalla, para poder
        // leerla de una captura sin importar en que momento se tome.
        char overlay[64];
        char n1[12];
        char n2[12];
        std::snprintf(overlay, sizeof(overlay), "T=%s P=%s",
                      intToText(n1, int(g_titleCycles)), intToText(n2, int(g_playingCycles)));
        drawText(fb, 2, 2, overlay, uint8_t(PAL_UI_ACCENT), 1);

        const uint32_t beforePresent = gbadbg::cycles();
#endif  // GBA_PROFILE

        platform.present(fb);

#ifdef GBA_PROFILE
        const uint32_t presentCycles = gbadbg::cycles() - beforePresent;
        const uint32_t frameCycles = gbadbg::cycles() - before;

        // Un frame de la pantalla de titulo (barato: solo texto) y uno ya
        // jugando (caro: DDA + sprites + minimapa + HUD) se registran una vez
        // cada uno, INCLUYENDO el present() (el volcado a VRAM y la espera de
        // vblank). Esta es la medida real que pide la fase 3, no una
        // estimacion de modelo.
        if (!loggedTitle && game.state() == Game::State::Title && frame > 10) {
            g_titleCycles = frameCycles;
            char msg[96];
            std::snprintf(msg, sizeof(msg), "TITLE frame cycles=%lu (%.1f fps eq)",
                          static_cast<unsigned long>(frameCycles),
                          frameCycles ? 16780000.0 / double(frameCycles) : 0.0);
            gbadbg::log(msg);
            char tb[96];
            std::snprintf(tb, sizeof(tb), "  TITLE screen=%lu copy=%lu vblank=%lu",
                          static_cast<unsigned long>(g_screenCycles),
                          static_cast<unsigned long>(g_copyCycles),
                          static_cast<unsigned long>(g_vblankCycles));
            gbadbg::log(tb);
            loggedTitle = true;
        }
        if (game.state() == Game::State::Playing) {
            ++playingFrames;
        } else {
            playingFrames = 0;
        }
        // Se salta el frame que hace newRun()/loadFloor(): esa carga es un
        // costo de una vez por piso detras de la transicion, no el costo por
        // frame en juego que esta fase quiere medir.
        if (!loggedPlaying && playingFrames == kProfileFrames) {
            g_playingCycles = frameCycles;
            g_presentCycles = presentCycles;
            char pmsg[96];
            std::snprintf(pmsg, sizeof(pmsg), "PRESENT cycles=%lu (%.0f%% del frame)",
                          static_cast<unsigned long>(presentCycles),
                          frameCycles ? 100.0 * double(presentCycles) / double(frameCycles) : 0.0);
            gbadbg::log(pmsg);
            char msg[96];
            std::snprintf(msg, sizeof(msg), "PLAYING frame cycles=%lu (%.1f fps eq)",
                          static_cast<unsigned long>(frameCycles),
                          frameCycles ? 16780000.0 / double(frameCycles) : 0.0);
            gbadbg::log(msg);
            char pb[128];
            std::snprintf(pb, sizeof(pb),
                          "  PLAY world=%lu spr=%lu mini=%lu hud=%lu copy=%lu vblank=%lu",
                          static_cast<unsigned long>(g_worldCycles),
                          static_cast<unsigned long>(g_spritesCycles),
                          static_cast<unsigned long>(g_minimapCycles),
                          static_cast<unsigned long>(g_hudCycles),
                          static_cast<unsigned long>(g_copyCycles),
                          static_cast<unsigned long>(g_vblankCycles));
            gbadbg::log(pb);
            char wb[96];
            std::snprintf(wb, sizeof(wb), "  WORLD bg=%lu ray=%lu tex=%lu",
                          static_cast<unsigned long>(g_bgCycles),
                          static_cast<unsigned long>(g_rayCycles),
                          static_cast<unsigned long>(g_texCycles));
            gbadbg::log(wb);
            loggedPlaying = true;
        }
#endif  // GBA_PROFILE

#if defined(GBA_PROFILE) || defined(GBA_AUTOSTART)
        ++frame;
#endif
    }
}
