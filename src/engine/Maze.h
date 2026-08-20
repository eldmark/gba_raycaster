#pragma once

#include <string>
#include <vector>

// Laberinto en unidades de celda. Cada caracter es una celda:
//   ' ' -> libre
//   '+' '-' '|' -> pared (el caracter elige el color / la textura)
class Maze {
public:
    bool load(const char* path);

    // Fuera de rango cuenta como pared solida, asi el raycaster y las
    // colisiones nunca se salen del mapa.
    char at(int x, int y) const;

    bool isWall(int x, int y) const { return at(x, y) != ' '; }

    int width() const { return width_; }
    int height() const { return static_cast<int>(rows_.size()); }

private:
    std::vector<std::string> rows_;
    int width_ = 0;  // la fila mas larga
};
