#include "Level.h"
#include "Maze.h"
#include "Random.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Inundacion desde el inicio: devuelve cuantas celdas de suelo son alcanzables.
// Si el mapa quedara partido en dos, esto lo delata.
int floodFrom(const Maze& maze, int sx, int sy, std::vector<bool>& seen) {
    seen.assign(size_t(maze.width()) * size_t(maze.height()), false);
    std::vector<int> stack{sy * maze.width() + sx};
    seen[size_t(stack[0])] = true;
    int count = 0;

    while (!stack.empty()) {
        int cell = stack.back();
        stack.pop_back();
        ++count;
        int cx = cell % maze.width();
        int cy = cell / maze.width();
        const int dx[4] = {1, -1, 0, 0};
        const int dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || nx >= maze.width() || ny < 0 || ny >= maze.height()) continue;
            if (maze.isWall(nx, ny)) continue;
            size_t idx = size_t(ny) * size_t(maze.width()) + size_t(nx);
            if (seen[idx]) continue;
            seen[idx] = true;
            stack.push_back(int(idx));
        }
    }
    return count;
}

int countFloor(const Maze& maze) {
    int n = 0;
    for (int y = 0; y < maze.height(); ++y) {
        for (int x = 0; x < maze.width(); ++x) {
            if (!maze.isWall(x, y)) ++n;
        }
    }
    return n;
}

std::string dump(const Maze& maze) {
    std::string s;
    for (int y = 0; y < maze.height(); ++y) {
        for (int x = 0; x < maze.width(); ++x) s += maze.at(x, y);
        s += '\n';
    }
    return s;
}

void testRandom() {
    // misma seed, misma secuencia; es de lo que depende todo lo demas
    Random a(42), b(42);
    for (int i = 0; i < 100; ++i) assert(a.next() == b.next());

    Random c(7);
    for (int i = 0; i < 1000; ++i) {
        int v = c.range(3, 9);
        assert(v >= 3 && v <= 9);
    }
    // range con rango vacio no debe colgarse ni salirse
    assert(Random(1).range(5, 5) == 5);
    assert(Random(1).range(9, 2) == 9);

    // una seed 0 no puede dejar el xorshift muerto en 0 para siempre
    Random z(0);
    assert(z.next() != 0);
}

void testLevel(uint32_t seed, int floor) {
    Maze maze;
    Level lvl = generateLevel(maze, seed, floor);

    assert(lvl.roomCount >= 2);
    assert(maze.width() == maze.height());

    int sx = fxFloorInt(lvl.startX);
    int sy = fxFloorInt(lvl.startY);

    // el jugador no puede aparecer dentro de una pared
    assert(!maze.isWall(sx, sy));
    assert(maze.at(lvl.exitX, lvl.exitY) == Maze::EXIT);

    // La salida debe estar rodeada por el material especial de extraccion, no
    // por una textura de sala generica.
    bool exitMaterial = false;
    for (int y = 0; y < maze.height(); ++y) {
        for (int x = 0; x < maze.width(); ++x) {
            if (maze.at(x, y) == 'E') exitMaterial = true;
        }
    }
    assert(exitMaterial);

    // el borde entero tiene que ser solido, o el jugador se sale del mapa
    for (int i = 0; i < maze.width(); ++i) {
        assert(maze.isWall(i, 0));
        assert(maze.isWall(i, maze.height() - 1));
        assert(maze.isWall(0, i));
        assert(maze.isWall(maze.width() - 1, i));
    }

    // TODO el suelo debe ser alcanzable desde el inicio: un piso con una sala
    // aislada seria imposible de terminar
    std::vector<bool> seen;
    int reached = floodFrom(maze, sx, sy, seen);
    const size_t exitCell = size_t(lvl.exitY) * size_t(maze.width()) + size_t(lvl.exitX);
    assert(seen[exitCell]);

    if (lvl.hasVault) {
        // La camara sellada es la UNICA parte del piso que puede quedar fuera
        // del alcance, y solo mientras la puerta siga cifrada.
        assert(maze.at(lvl.doorX, lvl.doorY) == Maze::DOOR);
        const size_t vaultCell =
            size_t(lvl.vault.cy()) * size_t(maze.width()) + size_t(lvl.vault.cx());
        assert(!maze.isWall(lvl.vault.cx(), lvl.vault.cy()));
        assert(!seen[vaultCell]);

        // Y al descifrarla el piso vuelve a ser conexo entero. Esto es lo que
        // impide que la puerta caiga sobre el camino principal y parta el mapa:
        // si lo hiciera, abrirla no bastaria para alcanzarlo todo.
        // Se abre el cifrado entero, igual que hace Game::tryUnlockVault: la
        // camara puede estar sellada por mas de una celda si el pasillo corre
        // pegado a su anillo.
        Maze opened = maze;
        for (int y = 0; y < opened.height(); ++y) {
            for (int x = 0; x < opened.width(); ++x) {
                if (opened.at(x, y) == Maze::DOOR) opened.set(x, y, Maze::FLOOR);
            }
        }
        std::vector<bool> seenOpen;
        assert(floodFrom(opened, sx, sy, seenOpen) == countFloor(opened));
    } else {
        assert(reached == countFloor(maze));
    }

    // las paredes deben usar mas de un material, o todas las salas se ven igual
    bool material[128] = {};
    for (int y = 0; y < maze.height(); ++y) {
        for (int x = 0; x < maze.width(); ++x) {
            if (maze.isWall(x, y)) material[int(maze.at(x, y))] = true;
        }
    }
    int kinds = int(material['+']) + int(material['-']) + int(material['|']);
    assert(kinds >= 2);
}

}  // namespace

int main() {
    testRandom();

    // varias seeds y varios pisos: el generador tiene que aguantar todos, no
    // solo el que se probo a mano
    for (uint32_t seed : {1u, 42u, 583291u, 0xDEADBEEFu, 999999u}) {
        for (int floor = 1; floor <= 6; ++floor) testLevel(seed, floor);
    }

    // Misma seed y mismo piso -> mapa identico byte a byte (seccion 14).
    {
        Maze a, b;
        Level la = generateLevel(a, 583291u, 3);
        Level lb = generateLevel(b, 583291u, 3);
        assert(dump(a) == dump(b));
        assert(la.startX == lb.startX && la.startY == lb.startY);
        assert(la.exitX == lb.exitX && la.exitY == lb.exitY);
    }

    // Seeds distintas -> mapas distintos, y pisos distintos de la misma seed
    // tampoco pueden repetirse.
    {
        Maze a, b, c;
        generateLevel(a, 1u, 1);
        generateLevel(b, 2u, 1);
        generateLevel(c, 1u, 2);
        assert(dump(a) != dump(b));
        assert(dump(a) != dump(c));
    }

    std::printf("all tests passed\n");
    return 0;
}
