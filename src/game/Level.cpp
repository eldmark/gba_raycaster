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

constexpr int MAX_ROOMS = MAX_LEVEL_ROOMS;
constexpr int ROOM_MIN = 4;
constexpr int ROOM_MAX = 8;

// Cada habitacion recibe un material distinto, asi que dos salas contiguas nunca
// se ven iguales. Ademas cubre el requisito de la entrega de que las paredes
// distintas del mapa tengan texturas distintas.
constexpr char MATERIALS[] = {'+', '-', '|'};
constexpr char EXIT_MATERIAL = 'E';
constexpr char VAULT_MATERIAL = 'V';

// Lado de la camara sellada. Pequena a proposito: es un premio, no una sala
// mas. Se intenta primero grande y se va encogiendo, porque a partir del piso 3
// el mapa se llena de pasillos y un hueco de 7x7 virgen deja de aparecer.

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

void paintExitBorder(Maze& maze, const Room& r) {
    for (int y = r.y - 1; y <= r.y + r.h; ++y) {
        for (int x = r.x - 1; x <= r.x + r.w; ++x) {
            if (maze.isWall(x, y)) maze.set(x, y, EXIT_MATERIAL);
        }
    }
}

// Cierto solo si el rectangulo y su anillo siguen siendo roca virgen. La
// camara se sella cortando su unico acceso, asi que hay que garantizar que
// ningun pasillo del camino principal la atraviese: si lo hiciera, la puerta
// partiria el mapa en dos y el piso dejaria de poder terminarse.
bool untouched(const Maze& maze, const Room& r) {
    for (int y = r.y - 1; y <= r.y + r.h; ++y) {
        for (int x = r.x - 1; x <= r.x + r.w; ++x) {
            if (maze.at(x, y) != Maze::WALL) return false;
        }
    }
    return true;
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

    // --- camara sellada -------------------------------------------------------
    // Va la ultima y sobre terreno intacto, de modo que la unica celda excavada
    // de su anillo sea la de su propio pasillo: esa es la puerta cifrada.
    Room vault{};
    bool hasVault = false;
    int doorX = 0, doorY = 0;
    for (int attempt = 0; attempt < 400 && placed > 0; ++attempt) {
        vault.w = vault.h = attempt < 200 ? 5 : (attempt < 320 ? 4 : 3);
        vault.x = rng.range(2, size - vault.w - 3);
        vault.y = rng.range(2, size - vault.h - 3);
        if (!untouched(maze, vault)) continue;

        carveRoom(maze, vault);
        const Room& anchor = rooms[rng.range(0, placed - 1)];
        carveCorridor(maze, vault.cx(), vault.cy(), anchor.cx(), anchor.cy(), rng);

        // El anillo tenia que ser roca entera, asi que lo unico abierto en el
        // es su propio pasillo. Se cierra ENTERO, no solo la primera celda: un
        // codo que corre pegado al anillo lo abre en varias casillas seguidas y
        // sellar una sola dejaba la camara accesible por el hueco de al lado.
        for (int y = vault.y - 1; y <= vault.y + vault.h; ++y) {
            for (int x = vault.x - 1; x <= vault.x + vault.w; ++x) {
                const bool ring = x == vault.x - 1 || x == vault.x + vault.w ||
                                  y == vault.y - 1 || y == vault.y + vault.h;
                if (!ring || maze.isWall(x, y)) continue;
                maze.set(x, y, Maze::DOOR);
                doorX = x;  // representante: el resto se abre con el
                doorY = y;
                hasVault = true;
            }
        }
        for (int y = vault.y - 1; y <= vault.y + vault.h; ++y) {
            for (int x = vault.x - 1; x <= vault.x + vault.w; ++x) {
                if (maze.at(x, y) == Maze::WALL) maze.set(x, y, VAULT_MATERIAL);
            }
        }
        // Se sale con o sin cerradura: repetir el intento excavaria una segunda
        // camara encima de la que ya quedo abierta en el mapa.
        break;
    }

    // La salida va en la ultima sala colocada, que por como se encadenan es la
    // mas lejana del inicio en numero de pasillos.
    const Room& first = rooms[0];
    const Room& last = rooms[placed - 1];
    // La sala de extraccion tiene una carcasa propia: se puede reconocer desde
    // el pasillo antes de ver la celda X del suelo.
    paintExitBorder(maze, last);
    maze.set(last.cx(), last.cy(), Maze::EXIT);

    // +0.5 celdas: el jugador arranca en el centro de la celda, no en la
    // esquina, o el radio de colision lo mete dentro de la pared.
    Level level{};
    level.startX = fxInt(first.cx()) + FX_ONE / 2;
    level.startY = fxInt(first.cy()) + FX_ONE / 2;
    level.exitX = last.cx();
    level.exitY = last.cy();
    level.roomCount = placed;
    for (int i = 0; i < placed; ++i) {
        level.rooms[i] = RoomBounds{rooms[i].x, rooms[i].y, rooms[i].w, rooms[i].h};
    }
    level.hasVault = hasVault;
    level.vault = RoomBounds{vault.x, vault.y, vault.w, vault.h};
    level.doorX = doorX;
    level.doorY = doorY;
    return level;
}
