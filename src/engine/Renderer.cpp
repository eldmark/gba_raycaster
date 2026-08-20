#include "Renderer.h"

#include <algorithm>

#include "Fixed.h"

#include "Framebuffer.h"
#include "Maze.h"
#include "Player.h"
#include "Raycaster.h"
#include "Textures.h"

namespace {

// El Rust oscurecia con (1 - d/500) sobre distancias en pixeles de mundo.
// En celdas, 500 px / 40 px por celda = 12.5 celdas. Se guarda el reciproco
// para que el sombreado sea una multiplicacion y no una division.
constexpr fx INV_SHADE_RANGE = fxFloat(1.0f / 12.5f);
constexpr fx MIN_LIGHT = fxFloat(0.25f);
constexpr fx SIDE_LIGHT = fxFloat(0.7f);

// Devuelve el NIVEL de la rampa, no un factor: el sombreado ya esta horneado en
// la paleta, asi que el bucle interno solo suma este entero al indice.
int shadeLevel(fx perpDist, int side) {
    fx light = FX_ONE - fxMul(perpDist, INV_SHADE_RANGE);
    light = std::clamp(light, MIN_LIGHT, FX_ONE);
    // las caras horizontales van mas oscuras, como en Wolf3D: da volumen sin
    // necesidad de iluminacion real.
    if (side == 1) light = fxMul(light, SIDE_LIGHT);
    return std::clamp(int((light * SHADE_LEVELS) >> FX_BITS), 0, SHADE_LEVELS - 1);
}

// Base de camara de Wolf3D: dir mira al frente, plane es perpendicular y mide
// tan(fov/2). rayDir = dir + plane * cameraX barre el FOV exacto, y de yapa
// deja el vector sin normalizar para que el t del DDA salga perpendicular.
struct Camera {
    fx dirX, dirY;
    fx planeX, planeY;
};

Camera cameraOf(const Player& p) {
    // p.fov ya viene como tan(fov/2): la tangente se calcula al crear al
    // jugador, no una vez por frame.
    fx dirX = fxCos(p.a);
    fx dirY = fxSin(p.a);
    return {dirX, dirY, -fxMul(dirY, p.fov), fxMul(dirX, p.fov)};
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

    // cameraX barre [-1, 1) de a pasos iguales. Incremental para no pagar una
    // division por columna: la unica que queda es la de la altura.
    const fx cameraStep = fxDiv(2 * FX_ONE, fxInt(w));
    fx cameraX = -FX_ONE;

    // TEX_SIZE/h es constante, asi que el avance en textura sale de una
    // multiplicacion por perpDist en vez de dividir entre la altura proyectada.
    const fx texPerDist = fxDiv(fxInt(TEX_SIZE), fxInt(h));

    for (int x = 0; x < w; ++x, cameraX += cameraStep) {
        fx rayX = cam.dirX + fxMul(cam.planeX, cameraX);
        fx rayY = cam.dirY + fxMul(cam.planeY, cameraX);

        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);

        // altura proyectada; equivale al BLOCK_SIZE*HEIGHT/d del Rust porque
        // alla d estaba en pixeles de mundo y aca perpDist esta en celdas.
        fx stakeHeight = fxDiv(fxInt(h), hit.perpDist);
        fx exactTop = fxInt(half) - (stakeHeight >> 1);
        int top = std::max(0, fxFloorInt(exactTop));
        int bottom = std::min(h, fxFloorInt(fxInt(half) + (stakeHeight >> 1)));

        // caras que miran a -X / -Y se espejan, si no la textura sale invertida
        // al rodear una esquina y se nota la costura.
        fx u = hit.wallX;
        if ((hit.side == 0 && rayX < 0) || (hit.side == 1 && rayY > 0)) {
            u = FX_ONE - 1 - u;
        }
        int texX = (u * TEX_SIZE) >> FX_BITS;

        const int texId = texIndex(hit.impact);
        const Texture& tex = textures()[texId];
        const uint8_t base = wallBase(texId, shadeLevel(hit.perpDist, hit.side));

        // avance en la textura por pixel de pantalla. texPos arranca desde
        // exactTop y no desde top, asi la textura no "resbala" cuando la pared
        // se sale por arriba de la pantalla.
        fx step = fxMul(hit.perpDist, texPerDist);
        fx texPos = fxMul(fxInt(top) - exactTop, step);

        const uint8_t* col = tex.px + texX;
        for (int y = top; y < bottom; ++y, texPos += step) {
            int texY = fxFloorInt(texPos) & (TEX_SIZE - 1);
            fb.setPixel(x, y, uint8_t(base + col[texY * TEX_SIZE] * SHADE_LEVELS));
        }
    }
}

void renderMinimap(Framebuffer& fb, const Maze& maze, const Player& player) {
    // La rubrica lo exige en una esquina, no al lado del mapa principal: se
    // dimensiona como fraccion de la pantalla para que ocupe lo mismo en el
    // escritorio que en los 240x160 de la GBA.
    constexpr int NUM_RAYS = 24;
    const int cell = std::max(2, fb.width() / (maze.width() * 6));
    const int margin = cell;

    auto toMapX = [&](fx cx) { return margin + ((cx * cell) >> FX_BITS); };
    auto toMapY = [&](fx cy) { return margin + ((cy * cell) >> FX_BITS); };

    fb.fillRect(margin - 1, margin - 1, maze.width() * cell + 2,
                maze.height() * cell + 2, PAL_MAP_BG);

    for (int j = 0; j < maze.height(); ++j) {
        for (int i = 0; i < maze.width(); ++i) {
            if (!maze.isWall(i, j)) continue;
            fb.fillRect(margin + i * cell, margin + j * cell, cell, cell,
                        PAL_MAP_WALL);
        }
    }

    const Camera cam = cameraOf(player);
    const int px = toMapX(player.x);
    const int py = toMapY(player.y);

    const fx cameraStep = fxDiv(2 * FX_ONE, fxInt(NUM_RAYS));
    fx cameraX = -FX_ONE;

    for (int i = 0; i < NUM_RAYS; ++i, cameraX += cameraStep) {
        fx rayX = cam.dirX + fxMul(cam.planeX, cameraX);
        fx rayY = cam.dirY + fxMul(cam.planeY, cameraX);
        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);

        // hit = pos + perpDist * rayDir, por definicion del DDA
        int hx = toMapX(player.x + fxMul(hit.perpDist, rayX));
        int hy = toMapY(player.y + fxMul(hit.perpDist, rayY));

        // linea con interpolacion, portada de Framebuffer::draw_line del Rust
        int steps = std::max({std::abs(hx - px), std::abs(hy - py), 1});
        for (int s = 0; s <= steps; ++s) {
            fb.setPixel(px + (hx - px) * s / steps, py + (hy - py) * s / steps,
                        PAL_MAP_RAY);
        }
    }

    fb.fillRect(px - 1, py - 1, 3, 3, PAL_MAP_PLAYER);
}
