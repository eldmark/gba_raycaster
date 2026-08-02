#include "Maze.h"
#include "Player.h"
#include "Raycaster.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

const char* kMazePath = "test_maze.txt";

void writeMaze() {
    FILE* f = std::fopen(kMazePath, "w");
    assert(f);
    std::fputs("+++\n+ +\n+++\n", f);
    std::fclose(f);
}

// Un rayo desde el centro de la celda libre choca a media celda en los 4 ejes.
void checkAxis(const Maze& maze, float dirX, float dirY, int expectedSide) {
    Hit h = castRay(maze, 1.5f, 1.5f, dirX, dirY);
    assert(std::fabs(h.perpDist - 0.5f) < 1e-4f);
    assert(h.impact == '+');
    assert(h.side == expectedSide);
    assert(h.wallX >= 0.0f && h.wallX < 1.0f);
}

}  // namespace

int main() {
    writeMaze();
    Maze maze;
    bool ok = maze.load(kMazePath);
    std::remove(kMazePath);
    assert(ok);
    assert(maze.width() == 3 && maze.height() == 3);

    // Port del test de laberinto/src/caster.rs: 20px con BLOCK_SIZE=40 = 0.5 celdas.
    checkAxis(maze, 1.0f, 0.0f, 0);
    checkAxis(maze, -1.0f, 0.0f, 0);
    checkAxis(maze, 0.0f, 1.0f, 1);
    checkAxis(maze, 0.0f, -1.0f, 1);

    // Diagonal a 45 grados: distancia finita y positiva.
    const float k = 0.70710678f;
    Hit d = castRay(maze, 1.5f, 1.5f, k, k);
    assert(d.perpDist > 0.0f && std::isfinite(d.perpDist));
    assert(d.impact == '+');
    assert(d.wallX >= 0.0f && d.wallX < 1.0f);

    // Barrido completo: wallX siempre en [0,1) y nada explota.
    for (int i = 0; i < 360; ++i) {
        float a = i * 3.14159265f / 180.0f;
        Hit h = castRay(maze, 1.5f, 1.5f, std::cos(a), std::sin(a));
        assert(h.wallX >= 0.0f && h.wallX < 1.0f);
        assert(h.perpDist > 0.0f && std::isfinite(h.perpDist));
    }

    // Arrancar dentro de una pared no puede colgar ni devolver distancia <= 0.
    Hit inside = castRay(maze, 0.5f, 0.5f, 1.0f, 0.0f);
    assert(inside.perpDist > 0.0f && std::isfinite(inside.perpDist));

    // Sin ojo de pez: mirando de frente a una pared plana, todas las columnas
    // deben devolver la MISMA perpDist, o la pared se ve curva. Es el unico
    // chequeo que atrapa una correccion de coseno faltante o aplicada de mas.
    // Se replica aqui la base de camara de Renderer.cpp a proposito.
    {
        float dirX = 0.0f, dirY = -1.0f;            // mirando hacia arriba (-Y)
        float half = std::tan((3.14159265f / 3.0f) * 0.5f);  // fov = 60 grados
        float planeX = -dirY * half, planeY = dirX * half;
        for (int x = 0; x < 64; ++x) {
            float cameraX = 2.0f * x / 64 - 1.0f;
            Hit h = castRay(maze, 1.5f, 1.5f, dirX + planeX * cameraX,
                            dirY + planeY * cameraX);
            assert(std::fabs(h.perpDist - 0.5f) < 1e-4f);
        }
    }

    assert(!collides(maze, 1.5f, 1.5f));
    assert(collides(maze, 1.5f, 0.5f));    // dentro de la pared de arriba
    assert(collides(maze, -1.0f, 1.5f));   // fuera del mapa

    std::printf("all tests passed\n");
    return 0;
}
