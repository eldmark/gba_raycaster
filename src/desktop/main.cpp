#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "Fixed.h"
#include "Framebuffer.h"
#include "Game.h"
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
    if (!platform.init(WIDTH, HEIGHT, "Roguelike GBA")) {
        platform.shutdown();
        return 1;
    }

    Framebuffer fb(WIDTH, HEIGHT);
    Game game;
    game.newRun(seed);
    std::printf("run con seed %u\n", seed);

    Input input;

    // el motor Rust movia por frame a 60 FPS fijos; aca se mide dt real y las
    // velocidades estan en unidades por segundo. El primer frame usa 1/60.
    fx dt = FX_ONE / 60;
    unsigned long long prevTicks = platform.ticksMs();
    unsigned long long fpsTicks = prevTicks;
    int frames = 0;
    Game::State shownState = Game::State::Playing;

    while (platform.pollInput(input)) {
        // terminada la run, START arranca otra con una seed nueva
        if (game.state() != Game::State::Playing && input.start) {
            seed = uint32_t(platform.ticksMs()) ^ (seed * 2654435761u);
            game.newRun(seed);
            std::printf("run nueva con seed %u\n", seed);
        }

        game.update(input, dt);

        renderWorld(fb, game.maze(), game.player());
        renderMinimap(fb, game.maze(), game.player());
        platform.present(fb);

        if (game.state() != shownState) {
            shownState = game.state();
            if (shownState == Game::State::Dead) {
                std::printf("RUN OVER  piso %d  bajas %d  seed %u\n", game.floor(),
                            game.kills(), game.seed());
            } else if (shownState == Game::State::Cleared) {
                std::printf("RUN COMPLETADA  bajas %d  seed %u\n", game.kills(),
                            game.seed());
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
                const char* tag = game.state() == Game::State::Dead      ? " - MUERTO"
                                  : game.state() == Game::State::Cleared ? " - COMPLETADO"
                                                                         : "";
                char title[96];
                std::snprintf(title, sizeof(title),
                              "Roguelike GBA - piso %d/%d - HP %d - %.0f FPS%s",
                              game.floor(), Game::FINAL_FLOOR, game.hp(),
                              frames * 1000.0f / float(elapsed), tag);
                platform.setTitle(title);
            }
            fpsTicks = now;
            frames = 0;
        }
    }

    platform.shutdown();
    return 0;
}
