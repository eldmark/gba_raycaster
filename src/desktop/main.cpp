#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "Audio.h"
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

    // El audio se engancha a lo que el juego ya expone -fogonazo, interferencia
    // de dano, aviso de recogida y estado- en vez de anadirle una cola de
    // eventos: la capa de juego sigue sin saber que existe el sonido, que es lo
    // que le permite compilar tal cual para GBA.
    Audio audio;
    audio.init(AUDIO_DIR);

    bool prevMuzzle = false, prevHurt = false, prevNotice = false;

    Input input;

    // Se mide dt real y las velocidades estan en unidades por SEGUNDO, no por
    // frame: con un frame fijo, el juego correria mas despacio en la consola
    // que en el escritorio. El primer frame usa 1/60 porque todavia no hay
    // nada que medir.
    fx dt = FX_ONE / 60;
    unsigned long long prevTicks = platform.ticksMs();
    unsigned long long fpsTicks = prevTicks;
    int frames = 0;
    // Ultimo valor medido, para poder dibujarlo en pantalla ademas de en el
    // titulo de la ventana: el titulo no se ve en una captura ni en un video.
    int shownFps = 0;
    Game::State shownState = Game::State::Title;

    while (platform.pollInput(input)) {
        // el paso entre pantallas lo lleva Game, no el bucle: asi la GBA hereda
        // el mismo flujo sin repetirlo
        const Game::State before = game.state();
        game.update(input, dt);

        const bool muzzle = game.muzzleFlash();
        const bool hurt = game.connectionLoss() > 0;
        const bool notice = game.pickupNotice() != nullptr;
        if (muzzle && !prevMuzzle) audio.play(Audio::Shot);
        if (hurt && !prevHurt) audio.play(Audio::Damage);
        if (notice && !prevNotice) audio.play(Audio::Pickup);
        prevMuzzle = muzzle;
        prevHurt = hurt;
        prevNotice = notice;

        if (game.state() != before) {
            if (game.state() == Game::State::Transition) audio.play(Audio::LevelUp);
            if (game.state() == Game::State::Cleared) audio.play(Audio::Victory);
        }
        // El tema de asalto acompana a la partida; el resto de pantallas, desde
        // el titulo hasta la de derrota, se quedan con el tema principal.
        audio.setTrack(game.state() == Game::State::Playing ||
                               game.state() == Game::State::Transition
                           ? Audio::Assault
                           : Audio::Menu);

        if (game.inWorld()) {
            renderWorld(fb, game.maze(), game.player());

            // los sprites van despues de las paredes: usan el z-buffer que
            // acaba de dejar renderWorld
            SpriteInstance sprites[Game::MAX_WORLD_SPRITES];
            int spriteCount = game.buildSprites(sprites, Game::MAX_ENEMIES);
            spriteCount += game.buildItemSprites(sprites + spriteCount,
                                                 Game::MAX_WORLD_SPRITES - spriteCount);
            renderSprites(fb, game.player(), sprites, spriteCount);

            renderMinimap(fb, game.maze(), game.player());
            renderMinimapEntities(fb, game.maze(), game.player(), sprites, spriteCount);
            drawHud(fb, game);
            // El CRT afecta tambien al HUD y al minimapa: la conexion que se
            // cae es la pantalla completa, no solo el mundo renderizado.
            renderConnectionLoss(fb, game.connectionLoss(), game.glitchPhase());
        } else {
            drawScreen(fb, game);
        }
        drawFps(fb, shownFps);
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
                shownFps = int(frames * 1000ull / elapsed);
                char title[96];
                std::snprintf(title, sizeof(title), "VIOLET HAT - %.0f FPS",
                              frames * 1000.0f / float(elapsed));
                platform.setTitle(title);
            }
            fpsTicks = now;
            frames = 0;
        }
    }

    audio.shutdown();
    platform.shutdown();
    return 0;
}
