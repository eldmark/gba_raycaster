#include "Level.h"

#include <algorithm>

#include "Random.h"

namespace {

struct Room {
    int x, y, w, h;
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
    bool overlaps(const Room& o) const {
        // se compara con un margen de 1 celda para que dos habitaciones nunca
        // queden pared con pared: si no, se leen como una sola sala grande
        return x - 1 <= o.x + o.w && o.x - 1 <= x + w &&
               y - 1 <= o.y + o.h && o.y - 1 <= y + h;
    }
};

constexpr int MAX_ROOMS = 12;
constexpr int ROOM_MIN = 4;
constexpr int ROOM_MAX = 8;

// Cada habitacion recibe un material distinto, asi que dos salas contiguas nunca
// se ven iguales. Ademas cubre el requisito de la entrega de que las paredes
// distintas del mapa tengan texturas distintas.
constexpr char MATERIALS[] = {'+', '-', '|'};

void carveRoom(Maze& maze, const Room& r) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) maze.set(x, y, Maze::FLOOR);
    }
}

// Solo pinta el material sobre celdas que siguen siendo pared: nunca tapa un
// suelo ya excavado ni el material de una sala vecina.
void paintBorder(Maze& maze, const Room& r, char material) {
    for (int y = r.y - 1; y <= r.y + r.h; ++y) {
        for (int x = r.x - 1; x <= r.x + r.w; ++x) {
            if (maze.at(x, y) == Maze::WALL) maze.set(x, y, material);
        }
    }
}

void carveRow(Maze& maze, int y, int xa, int xb) {
    for (int x = std::min(xa, xb); x <= std::max(xa, xb); ++x) {
        maze.set(x, y, Maze::FLOOR);
    }
}

void carveCol(Maze& maze, int x, int ya, int yb) {
    for (int y = std::min(ya, yb); y <= std::max(ya, yb); ++y) {
        maze.set(x, y, Maze::FLOOR);
    }
}

// Pasillo en L: un tramo recto, un codo, otro tramo recto. Cual va primero se
// sortea para que no todos los codos caigan hacia el mismo lado.
void carveCorridor(Maze& maze, int x0, int y0, int x1, int y1, Random& rng) {
    if (rng.chance(50)) {
        carveRow(maze, y0, x0, x1);  // codo en (x1, y0)
        carveCol(maze, x1, y0, y1);
    } else {
        carveCol(maze, x0, y0, y1);  // codo en (x0, y1)
        carveRow(maze, y1, x0, x1);
    }
}

}  // namespace

Level generateLevel(Maze& maze, uint32_t seed, int floor) {
    // La seed se mezcla con el piso para que el piso 2 de una run no sea igual
    // al piso 1 de otra que empezo con la seed siguiente.
    Random rng(seed * 2654435761u + uint32_t(floor) * 40503u);

    // El mapa crece con el piso, pero con tope: en GBA cada celda es un byte y
    // 48x48 son 2304, que es lo que conviene mantener en memoria.
    const int size = std::min(48, 24 + floor * 4);
    maze.reset(size, size, Maze::WALL);

    const int wanted = std::min(MAX_ROOMS, 5 + floor);
    Room rooms[MAX_ROOMS];
    int placed = 0;

    // Se intenta mas veces de las que hacen falta y se aceptan las que caben:
    // es mas simple que buscar un hueco valido y da salas repartidas igual.
    for (int attempt = 0; attempt < wanted * 12 && placed < wanted; ++attempt) {
        Room r;
        r.w = rng.range(ROOM_MIN, ROOM_MAX);
        r.h = rng.range(ROOM_MIN, ROOM_MAX);
        r.x = rng.range(1, size - r.w - 2);
        r.y = rng.range(1, size - r.h - 2);

        bool clash = false;
        for (int i = 0; i < placed && !clash; ++i) clash = r.overlaps(rooms[i]);
        if (clash) continue;

        // Conectar con la anterior ANTES de excavar mantiene el mapa siempre
        // conexo: cada sala nueva llega enganchada a la cadena.
        carveRoom(maze, r);
        if (placed > 0) {
            carveCorridor(maze, rooms[placed - 1].cx(), rooms[placed - 1].cy(),
                          r.cx(), r.cy(), rng);
        }
        rooms[placed++] = r;
    }

    for (int i = 0; i < placed; ++i) {
        paintBorder(maze, rooms[i], MATERIALS[i % 3]);
    }

    // La salida va en la ultima sala colocada, que por como se encadenan es la
    // mas lejana del inicio en numero de pasillos.
    const Room& first = rooms[0];
    const Room& last = rooms[placed - 1];
    maze.set(last.cx(), last.cy(), Maze::EXIT);

    // +0.5 celdas: el jugador arranca en el centro de la celda, no en la
    // esquina, o el radio de colision lo mete dentro de la pared.
    return Level{fxInt(first.cx()) + FX_ONE / 2,
                 fxInt(first.cy()) + FX_ONE / 2,
                 last.cx(), last.cy(), placed};
}
