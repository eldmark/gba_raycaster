#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include "Framebuffer.h"
#include "Maze.h"
#include "Player.h"
#include "Raycaster.h"
#include "Textures.h"

namespace {

// El Rust oscurecia con (1 - d/500) sobre distancias en pixeles de mundo.
// En celdas, 500 px / 40 px por celda = 12.5 celdas.
constexpr float SHADE_RANGE = 12.5f;

// Devuelve el NIVEL de la rampa, no un factor: el sombreado ya esta horneado en
// la paleta, asi que el bucle interno solo suma este entero al indice.
int shadeLevel(float perpDist, int side) {
    float light = std::clamp(1.0f - perpDist / SHADE_RANGE, 0.25f, 1.0f);
    // las caras horizontales van mas oscuras, como en Wolf3D: da volumen sin
    // necesidad de iluminacion real.
    if (side == 1) light *= 0.7f;
    return std::clamp(int(light * SHADE_LEVELS), 0, SHADE_LEVELS - 1);
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

    // cielo y piso en bandas de paleta: degradan sin costar mas que 2*BG_LEVELS
    // fillRect por frame, y en GBA el degradado se puede pasar a HBlank DMA.
    for (int b = 0; b < BG_LEVELS; ++b) {
        int y0 = half * b / BG_LEVELS;
        int y1 = half * (b + 1) / BG_LEVELS;
        fb.fillRect(0, y0, w, y1 - y0, uint8_t(PAL_SKY + b));

        int f0 = half + (h - half) * b / BG_LEVELS;
        int f1 = half + (h - half) * (b + 1) / BG_LEVELS;
        fb.fillRect(0, f0, w, f1 - f0, uint8_t(PAL_FLOOR + b));
    }

    for (int x = 0; x < w; ++x) {
        float cameraX = 2.0f * x / w - 1.0f;
        float rayX = cam.dirX + cam.planeX * cameraX;
        float rayY = cam.dirY + cam.planeY * cameraX;

        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);

        // altura proyectada; equivale al BLOCK_SIZE*HEIGHT/d del Rust porque
        // alla d estaba en pixeles de mundo y aca perpDist esta en celdas.
        float stakeHeight = h / hit.perpDist;
        float exactTop = half - stakeHeight * 0.5f;
        int top = std::max(0, int(exactTop));
        int bottom = std::min(h, int(half + stakeHeight * 0.5f));

        // caras que miran a -X / -Y se espejan, si no la textura sale invertida
        // al rodear una esquina y se nota la costura.
        float u = hit.wallX;
        if ((hit.side == 0 && rayX < 0.0f) || (hit.side == 1 && rayY > 0.0f)) {
            u = 1.0f - u;
        }
        int texX = std::min(int(u * TEX_SIZE), TEX_SIZE - 1);

        const int texId = texIndex(hit.impact);
        const Texture& tex = textures()[texId];
        const uint8_t base = wallBase(texId, shadeLevel(hit.perpDist, hit.side));

        // avance en la textura por pixel de pantalla. texPos arranca desde
        // exactTop y no desde top, asi la textura no "resbala" cuando la pared
        // se sale por arriba de la pantalla.
        float step = float(TEX_SIZE) / stakeHeight;
        float texPos = (top - exactTop) * step;

        for (int y = top; y < bottom; ++y) {
            int texY = int(texPos) & (TEX_SIZE - 1);
            texPos += step;
            fb.setPixel(x, y, uint8_t(base + tex.px[texY * TEX_SIZE + texX] * SHADE_LEVELS));
        }
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
                int(maze.height() * CELL) + 4, PAL_MAP_BG);

    for (int j = 0; j < maze.height(); ++j) {
        for (int i = 0; i < maze.width(); ++i) {
            if (!maze.isWall(i, j)) continue;
            fb.fillRect(toMapX(float(i)), toMapY(float(j)), int(CELL) + 1,
                        int(CELL) + 1, PAL_MAP_WALL);
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
                        PAL_MAP_RAY);
        }
    }

    fb.fillRect(px - 2, py - 2, 4, 4, PAL_MAP_PLAYER);
}
