#include "Maze.h"

#include <fstream>

bool Maze::load(const char* path) {
    std::ifstream file(path);
    if (!file) return false;

    rows_.clear();
    width_ = 0;
    std::string line;
    while (std::getline(file, line)) {
        // el archivo puede venir con CRLF
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (static_cast<int>(line.size()) > width_) width_ = static_cast<int>(line.size());
        rows_.push_back(line);
    }
    return !rows_.empty();
}

char Maze::at(int x, int y) const {
    if (y < 0 || y >= height() || x < 0) return '+';
    const std::string& row = rows_[static_cast<size_t>(y)];
    // las filas son irregulares (no se rellenan con espacios): pasado el final
    // de ESTA fila es pared, no fuera del mapa
    if (x >= static_cast<int>(row.size())) return '+';
    return row[static_cast<size_t>(x)];
}
