#include "Maze.h"

#include <fstream>
#include <string>

void Maze::reset(int w, int h, char fill) {
    width_ = w;
    height_ = h;
    cells_.assign(size_t(w) * size_t(h), fill);
}

void Maze::set(int x, int y, char c) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    cells_[size_t(y) * size_t(width_) + size_t(x)] = c;
}

bool Maze::load(const char* path) {
    std::ifstream file(path);
    if (!file) return false;

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
