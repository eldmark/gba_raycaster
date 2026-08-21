#include "Nav.h"

#include <cassert>
#include <utility>

namespace {
// Orden fijo: derecha, izquierda, abajo, arriba.
constexpr int DX[4] = {1, -1, 0, 0};
constexpr int DY[4] = {0, 0, 1, -1};
constexpr int OPPOSITE[4] = {1, 0, 3, 2};
}  // namespace

void Nav::beginRebuild(const Maze& maze, int px, int py) {
    width_ = maze.width();
    height_ = maze.height();
    assert(width_ <= kStride && height_ <= kStride);
    buildMaze_ = &maze;
    queueHead_ = queueTail_ = 0;
    building_ = false;

    // Solo se limpia la sub-rejilla que este piso usa de verdad: el resto del
    // buffer de 64x64 no se lee nunca porque step() y el BFS acotan por
    // width_/height_ antes de indexar.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) dirBack_[(y << 6) + x] = NONE;
    }

    if (px < 0 || px >= width_ || py < 0 || py >= height_ || maze.isWall(px, py)) {
        // Nada alcanzable desde ahi: el campo queda vacio, pero igual hay que
        // intercambiar o step() seguiria leyendo el campo del piso anterior.
        std::swap(dir_, dirBack_);
        return;
    }

    // la celda del jugador queda marcada como visitada con una direccion que
    // nadie va a leer: los guardianes que llegan ahi ya estan encima
    dirBack_[(py << 6) + px] = 0;
    queue_[queueTail_++] = uint16_t(px | (py << 8));
    building_ = true;
}

void Nav::tick(int budget) {
    if (!building_) return;

    while (budget-- > 0 && queueHead_ < queueTail_) {
        const uint16_t packed = queue_[queueHead_++];
        const int cx = packed & 0xFF;
        const int cy = packed >> 8;

        for (int d = 0; d < 4; ++d) {
            const int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
            if (buildMaze_->isWall(nx, ny)) continue;

            const int idx = (ny << 6) + nx;
            if (dirBack_[idx] != NONE) continue;

            // el vecino apunta de vuelta a la celda de la que se le alcanzo,
            // que es la que esta mas cerca del jugador
            dirBack_[idx] = uint8_t(OPPOSITE[d]);
            queue_[queueTail_++] = uint16_t(nx | (ny << 8));
        }
    }

    if (queueHead_ >= queueTail_) {
        // Cola vacia: el campo de fondo ya es el campo completo. Los
        // guardianes solo ven este intercambio, nunca un campo a medio hacer.
        std::swap(dir_, dirBack_);
        building_ = false;
    }
}

void Nav::rebuild(const Maze& maze, int px, int py) {
    beginRebuild(maze, px, py);
    // Presupuesto de sobra: nunca hay mas celdas en cola que width_*height_,
    // asi que esto agota el BFS entero en una sola llamada. Es la ruta que
    // usan Game::loadFloor y Game::tryUnlockVault, donde la latencia importa
    // mas que repartir el pico (fase 1.2 del plan de port).
    tick(kCells);
}

bool Nav::step(int x, int y, int& dx, int& dy) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
    const uint8_t d = dir_[(y << 6) + x];
    if (d == NONE) return false;
    dx = DX[d];
    dy = DY[d];
    return true;
}
