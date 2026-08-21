#include "Debug.h"
#include "Fixed.h"
#include "Framebuffer.h"
#include "Game.h"
#include "Hud.h"
#include "Platform.h"
#include "Renderer.h"

#include <cstdio>

constexpr int WIDTH = 240;
constexpr int HEIGHT = 160;

// El framebuffer del juego vive en EWRAM, no en VRAM: el modo 4 rechaza
// escrituras de 8 bits (strb duplica el byte en las dos mitades del
// halfword), y setPixel/fillRect/shiftRow del motor escriben byte a byte. La
// salida correcta para esta fase es renderizar aqui y que Platform::present
// vuelque a VRAM con escrituras de 16 bits emparejadas; hacerlo mas barato
// (DMA, pares nativos en el propio bucle de render) es la fase 4.
static uint8_t g_fbBuf[WIDTH * HEIGHT];

// Cuantos frames medir al arrancar antes de dejar de imprimir. La pantalla de
// titulo y una partida ya en curso cuestan distinto (mas o menos sprites,
// HUD, minimapa), asi que se registran los dos por separado.
constexpr int kProfileFrames = 90;

int main() {
    Platform platform;
    platform.init(WIDTH, HEIGHT, "VIOLET HAT");

    Framebuffer fb(WIDTH, HEIGHT, g_fbBuf);
    Game game;
    // No hay RTC accesible sin hardware extra ni argv en un cartucho: semilla
    // fija. Reproducible es mejor que aleatorio de verdad para poder comparar
    // mediciones entre corridas.
    game.setSeed(12345u);

    Input input;
    fx dt = FX_ONE / 60;

    int frame = 0;
    int playingFrames = 0;  // frames consecutivos en State::Playing
    bool loggedTitle = false;
    bool loggedPlaying = false;

    for (;;) {
        platform.pollInput(input);

#ifdef GBA_AUTOSTART
        // ponytail: sin xdotool/lua en este entorno no hay forma de inyectar
        // botones en mgba-sdl headless. Este pulso sintetico de START solo
        // existe en el target "profile" del Makefile (GBA_AUTOSTART) para
        // poder medir el frame de juego sin manos; el .gba normal no lo
        // lleva y responde solo al KEYPAD real.
        if (frame == 30 || frame == 90) input.start = true;
#endif

        const uint32_t before = gbadbg::cycles();

        game.update(input, dt);

        if (game.state() == Game::State::Playing) {
            renderWorld(fb, game.maze(), game.player());

            SpriteInstance sprites[Game::MAX_WORLD_SPRITES];
            int spriteCount = game.buildSprites(sprites, Game::MAX_ENEMIES);
            spriteCount += game.buildItemSprites(sprites + spriteCount,
                                                 Game::MAX_WORLD_SPRITES - spriteCount);
            renderSprites(fb, game.player(), sprites, spriteCount);

            renderMinimap(fb, game.maze(), game.player());
            renderMinimapEntities(fb, game.maze(), game.player(), sprites, spriteCount);
            drawHud(fb, game);
            renderConnectionLoss(fb, game.connectionLoss(), game.glitchPhase());
        } else {
            drawScreen(fb, game);
        }

        platform.present(fb);

        const uint32_t frameCycles = gbadbg::cycles() - before;

        // Un frame de la pantalla de titulo (barato: solo texto) y uno ya
        // jugando (caro: DDA + sprites + minimapa + HUD) se registran una vez
        // cada uno. Esta es la medida real que pide la fase 3, no una
        // estimacion de modelo.
        if (!loggedTitle && game.state() == Game::State::Title && frame > 10) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "TITLE frame cycles=%lu (%.1f fps eq)",
                          static_cast<unsigned long>(frameCycles),
                          frameCycles ? 16780000.0 / double(frameCycles) : 0.0);
            gbadbg::log(msg);
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
            char msg[96];
            std::snprintf(msg, sizeof(msg), "PLAYING frame cycles=%lu (%.1f fps eq)",
                          static_cast<unsigned long>(frameCycles),
                          frameCycles ? 16780000.0 / double(frameCycles) : 0.0);
            gbadbg::log(msg);
            loggedPlaying = true;
        }

        ++frame;
    }
}
