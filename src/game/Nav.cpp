#include "Nav.h"

namespace {
// Orden fijo: derecha, izquierda, abajo, arriba.
constexpr int DX[4] = {1, -1, 0, 0};
constexpr int DY[4] = {0, 0, 1, -1};
constexpr int OPPOSITE[4] = {1, 0, 3, 2};
}  // namespace

void Nav::rebuild(const Maze& maze, int px, int py) {
    width_ = maze.width();
    height_ = maze.height();
    dir_.assign(size_t(width_) * size_t(height_), NONE);

    if (px < 0 || px >= width_ || py < 0 || py >= height_) return;
    if (maze.isWall(px, py)) return;

    // Cola sobre un vector con indice de lectura: una std::queue haria una
    // reserva por celda y esto se ejecuta dentro del bucle de juego.
    std::vector<int> queue;
    queue.reserve(dir_.size());
    const int start = py * width_ + px;
    // la celda del jugador queda marcada como visitada con una direccion que
    // nadie va a leer: los guardianes que llegan ahi ya estan encima
    dir_[size_t(start)] = 0;
    queue.push_back(start);

    for (size_t head = 0; head < queue.size(); ++head) {
        const int cell = queue[head];
        const int cx = cell % width_;
        const int cy = cell / width_;

        for (int d = 0; d < 4; ++d) {
            const int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
            if (maze.isWall(nx, ny)) continue;

            const size_t idx = size_t(ny) * size_t(width_) + size_t(nx);
            if (dir_[idx] != NONE) continue;

            // el vecino apunta de vuelta a la celda de la que se le alcanzo,
            // que es la que esta mas cerca del jugador
            dir_[idx] = uint8_t(OPPOSITE[d]);
            queue.push_back(int(idx));
        }
    }
}

bool Nav::step(int x, int y, int& dx, int& dy) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
    const uint8_t d = dir_[size_t(y) * size_t(width_) + size_t(x)];
    if (d == NONE) return false;
    dx = DX[d];
    dy = DY[d];
    return true;
}
