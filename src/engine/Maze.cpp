#include "Maze.h"

#include <cassert>
#include <fstream>
#include <string>
#include <vector>

void Maze::reset(int w, int h, char fill) {
    // Sin heap dinamico decente en GBA no hay reserva que valga: si el mapa no
    // cabe en el stride fijo, es un bug del generador, no un caso a soportar.
    assert(w >= 0 && h >= 0 && w <= kStride && h <= kStride);
    width_ = w;
    height_ = h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) cells_[(y << 6) + x] = fill;
    }
}

void Maze::set(int x, int y, char c) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    cells_[(y << 6) + x] = c;
}

bool Maze::load(const char* path) {
    std::ifstream file(path);
    if (!file) return false;

    // Esto solo lo usa el cargador de mapas de prueba en escritorio: el
    // generador real nunca pasa por aqui, asi que un vector temporal de
    // lineas no entra en el bucle de juego ni en el port.
    std::vector<std::string> rows;
    size_t widest = 0;
    std::string line;
    while (std::getline(file, line)) {
        // el archivo puede venir con CRLF
        if (!line.empty() && line.back() == '\r') line.pop_back();
        widest = std::max(widest, line.size());
        rows.push_back(line);
    }
    if (rows.empty()) return false;
    if (widest > size_t(kStride) || rows.size() > size_t(kStride)) return false;

    // las filas del archivo son irregulares; lo que falta al final de una fila
    // corta es pared, no fuera del mapa
    reset(int(widest), int(rows.size()), WALL);
    for (size_t y = 0; y < rows.size(); ++y) {
        for (size_t x = 0; x < rows[y].size(); ++x) {
            set(int(x), int(y), rows[y][x]);
        }
    }
    return true;
}
