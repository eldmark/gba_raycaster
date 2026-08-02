#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include "Framebuffer.h"
#include "Maze.h"
#include "Player.h"
#include "Raycaster.h"

namespace {

// Colores portados de wall_color() en laberinto/src/main.rs
uint32_t wallColor(char impact) {
    switch (impact) {
        case '+': return rgb(102, 191, 255);  // SKYBLUE
        case '-': return rgb(0, 121, 241);    // BLUE
        case '|': return rgb(0, 82, 172);     // DARKBLUE
        default:  return rgb(130, 130, 130);  // GRAY
    }
}

// El Rust oscurecia con (1 - d/500) sobre distancias en pixeles de mundo.
// En celdas, 500 px / 40 px por celda = 12.5 celdas.
constexpr float SHADE_RANGE = 12.5f;

float distanceShade(float perpDist) {
    return std::clamp(1.0f - perpDist / SHADE_RANGE, 0.25f, 1.0f);
}

// Base de camara de Wolf3D: dir mira al frente, plane es perpendicular y mide
// tan(fov/2). rayDir = dir + plane * cameraX barre el FOV exacto, y de yapa
// deja el vector sin normalizar para que el t del DDA salga perpendicular.
struct Camera {
    float dirX, dirY;
    float planeX, planeY;
};

Camera cameraOf(const Player& p) {
    float dirX = std::cos(p.a);
    float dirY = std::sin(p.a);
    float half = std::tan(p.fov * 0.5f);
    return {dirX, dirY, -dirY * half, dirX * half};
}

}  // namespace

void renderWorld(Framebuffer& fb, const Maze& maze, const Player& player) {
    const int w = fb.width();
    const int h = fb.height();
    const int half = h / 2;
    const Camera cam = cameraOf(player);

    fb.fillRect(0, 0, w, half, rgb(40, 40, 60));           // cielo
    fb.fillRect(0, half, w, h - half, rgb(70, 60, 50));    // piso

    for (int x = 0; x < w; ++x) {
        float cameraX = 2.0f * x / w - 1.0f;
        float rayX = cam.dirX + cam.planeX * cameraX;
        float rayY = cam.dirY + cam.planeY * cameraX;

        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);

        // altura proyectada; equivale al BLOCK_SIZE*HEIGHT/d del Rust porque
        // alla d estaba en pixeles de mundo y aca perpDist esta en celdas.
        float stakeHeight = h / hit.perpDist;
        int top = std::max(0, int(half - stakeHeight * 0.5f));
        int bottom = std::min(h, int(half + stakeHeight * 0.5f));

        uint32_t color = shade(wallColor(hit.impact), distanceShade(hit.perpDist));
        fb.fillRect(x, top, 1, bottom - top, color);
    }
}

void renderMinimap(Framebuffer& fb, const Maze& maze, const Player& player) {
    // el Rust escalaba 0.18 sobre celdas de 40 px -> 7.2 px por celda
    constexpr float CELL = 7.2f;
    constexpr int MARGIN = 10;
    constexpr int NUM_RAYS = 50;

    auto toMapX = [](float cx) { return MARGIN + int(cx * CELL); };
    auto toMapY = [](float cy) { return MARGIN + int(cy * CELL); };

    fb.fillRect(MARGIN - 2, MARGIN - 2, int(maze.width() * CELL) + 4,
                int(maze.height() * CELL) + 4, rgb(0, 0, 0));

    for (int j = 0; j < maze.height(); ++j) {
        for (int i = 0; i < maze.width(); ++i) {
            if (!maze.isWall(i, j)) continue;
            fb.fillRect(toMapX(float(i)), toMapY(float(j)), int(CELL) + 1,
                        int(CELL) + 1, rgb(102, 191, 255));
        }
    }

    const Camera cam = cameraOf(player);
    int px = toMapX(player.x);
    int py = toMapY(player.y);

    for (int i = 0; i < NUM_RAYS; ++i) {
        float cameraX = 2.0f * i / NUM_RAYS - 1.0f;
        float rayX = cam.dirX + cam.planeX * cameraX;
        float rayY = cam.dirY + cam.planeY * cameraX;
        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);

        // hit = pos + perpDist * rayDir, por definicion del DDA
        int hx = toMapX(player.x + hit.perpDist * rayX);
        int hy = toMapY(player.y + hit.perpDist * rayY);

        // linea con interpolacion, portada de Framebuffer::draw_line del Rust
        int steps = std::max({std::abs(hx - px), std::abs(hy - py), 1});
        for (int s = 0; s <= steps; ++s) {
            float t = float(s) / steps;
            fb.setPixel(int(px + (hx - px) * t), int(py + (hy - py) * t),
                        rgb(230, 41, 55));
        }
    }

    fb.fillRect(px - 2, py - 2, 4, 4, rgb(253, 249, 0));
}
