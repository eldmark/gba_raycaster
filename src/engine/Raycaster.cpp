#include "Raycaster.h"

namespace {
// Direccion con componente casi 0: la distancia entre cruces de ese eje es
// "infinita". 4096 celdas es mas ancho que cualquier mapa y, a diferencia de
// INT32_MAX, se puede multiplicar por una fraccion sin desbordar el 16.16.
constexpr fx kHuge = fxInt(4096);

// Por debajo de esto, 1/dir ya no cabe: se trata como componente nula.
constexpr fx kMinDir = FX_ONE / 4096;

// Corte defensivo: at() devuelve '+' fuera del mapa, asi que el DDA ya termina
// solo, pero una posicion degenerada no debe poder colgar el frame.
constexpr int kMaxSteps = 256;
}  // namespace

Hit castRay(const Maze& maze, fx posX, fx posY, fx dirX, fx dirY) {
    int mapX = fxFloorInt(posX);
    int mapY = fxFloorInt(posY);

    // 2 de las 3 divisiones que costaba cada columna. Ahora son reciprocos de
    // 32 bits: mismo resultado salvo 1 ulp, a un cuarto del precio.
    fx absX = fxAbs(dirX);
    fx absY = fxAbs(dirY);
    fx deltaDistX = (absX < kMinDir) ? kHuge : fxRecip(absX);
    fx deltaDistY = (absY < kMinDir) ? kHuge : fxRecip(absY);

    int stepX, stepY;
    fx sideDistX, sideDistY;

    if (dirX < 0) {
        stepX = -1;
        sideDistX = fxMul(fxFrac(posX), deltaDistX);
    } else {
        stepX = 1;
        sideDistX = fxMul(FX_ONE - fxFrac(posX), deltaDistX);
    }
    if (dirY < 0) {
        stepY = -1;
        sideDistY = fxMul(fxFrac(posY), deltaDistY);
    } else {
        stepY = 1;
        sideDistY = fxMul(FX_ONE - fxFrac(posY), deltaDistY);
    }

    int side = 0;
    bool hit = false;
    for (int i = 0; i < kMaxSteps && !hit; ++i) {
        if (sideDistX < sideDistY) {
            sideDistX += deltaDistX;
            mapX += stepX;
            side = 0;
        } else {
            sideDistY += deltaDistY;
            mapY += stepY;
            side = 1;
        }
        hit = maze.isWall(mapX, mapY);
    }

    // Restar el ultimo delta deshace el paso que se acaba de dar: queda la
    // distancia hasta la cara, ya proyectada sobre la direccion de camara
    // (sin ojo de pez). El llamador NO debe multiplicar por ningun coseno.
    fx perpDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
    if (perpDist < kMinDist) perpDist = kMinDist;

    fx wallX = (side == 0) ? (posY + fxMul(perpDist, dirY))
                           : (posX + fxMul(perpDist, dirX));
    wallX = fxFrac(wallX);  // enmascarar ya da el [0,1) tambien con negativos

    return Hit{perpDist, maze.at(mapX, mapY), side, wallX, mapX, mapY};
}
