#include "Raycaster.h"

#include <cmath>

namespace {
// Direccion con componente 0: la distancia entre cruces de ese eje es infinita.
constexpr float kHuge = 1e30f;
// El llamador divide entre perpDist; nunca dejarlo llegar a 0.
constexpr float kMinDist = 1e-4f;
// Corte defensivo: at() devuelve '+' fuera del mapa, asi que el DDA ya termina
// solo, pero una posicion degenerada no debe poder colgar el frame.
constexpr int kMaxSteps = 256;
}  // namespace

Hit castRay(const Maze& maze, float posX, float posY, float dirX, float dirY) {
    int mapX = static_cast<int>(std::floor(posX));
    int mapY = static_cast<int>(std::floor(posY));

    float deltaDistX = (dirX == 0.0f) ? kHuge : std::fabs(1.0f / dirX);
    float deltaDistY = (dirY == 0.0f) ? kHuge : std::fabs(1.0f / dirY);

    int stepX, stepY;
    float sideDistX, sideDistY;

    if (dirX < 0.0f) {
        stepX = -1;
        sideDistX = (posX - mapX) * deltaDistX;
    } else {
        stepX = 1;
        sideDistX = (mapX + 1.0f - posX) * deltaDistX;
    }
    if (dirY < 0.0f) {
        stepY = -1;
        sideDistY = (posY - mapY) * deltaDistY;
    } else {
        stepY = 1;
        sideDistY = (mapY + 1.0f - posY) * deltaDistY;
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
    float perpDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
    if (!(perpDist > kMinDist)) perpDist = kMinDist;  // atrapa NaN tambien

    float wallX = (side == 0) ? (posY + perpDist * dirY) : (posX + perpDist * dirX);
    wallX -= std::floor(wallX);

    return Hit{perpDist, maze.at(mapX, mapY), side, wallX, mapX, mapY};
}
