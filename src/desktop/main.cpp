#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "Fixed.h"
#include "Framebuffer.h"
#include "Game.h"
#include "Hud.h"
#include "Platform.h"
#include "Renderer.h"

constexpr int WIDTH = 900;
constexpr int HEIGHT = 600;

int main(int argc, char** argv) {
    // La seed se puede fijar por linea de comandos para reproducir una run
    // exacta (seccion 14 del PROJECT.md); sin argumento, cada partida es nueva.
    uint32_t seed = (argc > 1) ? uint32_t(std::strtoul(argv[1], nullptr, 10))
                               : uint32_t(std::time(nullptr));

    Platform platform;
    if (!platform.init(WIDTH, HEIGHT, "VIOLET HAT")) {
        platform.shutdown();
        return 1;
    }

    Framebuffer fb(WIDTH, HEIGHT);
    Game game;
    // se entra por la pantalla de bienvenida; la semilla queda cargada para la
    // primera run
    game.setSeed(seed);
    std::printf("semilla inicial %u\n", seed);

    Input input;

    // el motor Rust movia por frame a 60 FPS fijos; aca se mide dt real y las
    // velocidades estan en unidades por segundo. El primer frame usa 1/60.
    fx dt = FX_ONE / 60;
    unsigned long long prevTicks = platform.ticksMs();
    unsigned long long fpsTicks = prevTicks;
    int frames = 0;
    Game::State shownState = Game::State::Title;

    while (platform.pollInput(input)) {
        // el paso entre pantallas lo lleva Game, no el bucle: asi la GBA hereda
        // el mismo flujo sin repetirlo
        game.update(input, dt);

        if (game.state() == Game::State::Playing) {
            renderWorld(fb, game.maze(), game.player());

            // los sprites van despues de las paredes: usan el z-buffer que
            // acaba de dejar renderWorld
            SpriteInstance sprites[Game::MAX_ENEMIES];
            renderSprites(fb, game.player(), sprites,
                          game.buildSprites(sprites, Game::MAX_ENEMIES));

            renderMinimap(fb, game.maze(), game.player());
            drawHud(fb, game);
        } else {
            drawScreen(fb, game);
        }
        platform.present(fb);

        if (game.state() != shownState) {
            shownState = game.state();
            if (shownState == Game::State::Dead ||
                shownState == Game::State::Cleared) {
                std::printf("%s  archivo %d  bajas %d  puntos %d  seed %u\n",
                            shownState == Game::State::Cleared ? "EXTRACCION COMPLETA"
                                                               : "CONEXION PERDIDA",
                            game.floor(), game.kills(), game.score(), game.seed());
            }
        }

        unsigned long long now = platform.ticksMs();
        fx measured = fx((now - prevTicks) * FX_ONE / 1000);
        // se descarta un dt absurdo (arrastre de ventana, breakpoint) para que
        // el jugador no se teletransporte atravesando una pared
        if (measured > 0 && measured < FX_ONE / 4) dt = measured;
        prevTicks = now;

        if (++frames >= 30) {
            unsigned long long elapsed = now - fpsTicks;
            if (elapsed > 0) {
                char title[96];
                std::snprintf(title, sizeof(title), "VIOLET HAT - %.0f FPS",
                              frames * 1000.0f / float(elapsed));
                platform.setTitle(title);
            }
            fpsTicks = now;
            frames = 0;
        }
    }

    platform.shutdown();
    return 0;
}
