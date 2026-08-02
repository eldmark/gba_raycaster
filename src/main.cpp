#include <cstdio>

#include "Framebuffer.h"
#include "Maze.h"
#include "Platform.h"
#include "Player.h"
#include "Renderer.h"

constexpr int WIDTH = 900;
constexpr int HEIGHT = 600;
constexpr float PI = 3.14159265358979f;

int main(int argc, char** argv) {
    const char* mazePath = (argc > 1) ? argv[1] : "maze.txt";

    Maze maze;
    if (!maze.load(mazePath)) {
        std::fprintf(stderr, "no se pudo cargar el laberinto: %s\n", mazePath);
        return 1;
    }

    Platform platform;
    if (!platform.init(WIDTH, HEIGHT, "Laberinto")) {
        platform.shutdown();
        return 1;
    }

    Framebuffer fb(WIDTH, HEIGHT);
    Player player{1.5f, 1.5f, PI / 3.0f, PI / 3.0f};
    Input input;

    // el motor Rust movia por frame a 60 FPS fijos; aca se mide dt real y las
    // velocidades estan en unidades por segundo. El primer frame usa 1/60.
    float dt = 1.0f / 60.0f;
    unsigned long long prevTicks = platform.ticksMs();
    unsigned long long fpsTicks = prevTicks;
    int frames = 0;

    while (platform.pollInput(input)) {
        updatePlayer(player, input, maze, dt);

        renderWorld(fb, maze, player);
        renderMinimap(fb, maze, player);
        platform.present(fb);

        unsigned long long now = platform.ticksMs();
        float measured = (now - prevTicks) / 1000.0f;
        // se descarta un dt absurdo (arrastre de ventana, breakpoint) para que
        // el jugador no se teletransporte atravesando una pared
        if (measured > 0.0f && measured < 0.25f) dt = measured;
        prevTicks = now;

        if (++frames >= 30) {
            unsigned long long elapsed = now - fpsTicks;
            if (elapsed > 0) {
                char title[64];
                std::snprintf(title, sizeof(title), "Laberinto - %.0f FPS",
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
