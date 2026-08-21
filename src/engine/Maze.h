#pragma once

#include <vector>

// Rejilla de celdas del mundo. Plana y de tamano fijo: es la forma que necesita
// el generador para escribir y la unica que tiene sentido en la GBA.
//
// Convencion de caracteres (seccion 12 del PROJECT.md):
//   ' ' '.'  -> suelo libre
//   'X'      -> salida (se pisa, no es pared)
//   '#'      -> pared generica
//   '+' '-' '|' -> pared con textura propia (ver texIndex en Textures.h)
//   'E'      -> carcasa de la sala de extraccion
//   'V'      -> carcasa de la camara sellada
//   'D'      -> puerta cifrada: pared solida hasta que se usa la llave
class Maze {
public:
    static constexpr char WALL = '#';
    static constexpr char FLOOR = '.';
    static constexpr char EXIT = 'X';
    // Puerta cifrada de la camara sellada. Es pared hasta que el juego la
    // reemplaza por suelo al descifrarla con la llave del archivo.
    static constexpr char DOOR = 'D';

    bool load(const char* path);

    // Deja el mapa de w x h relleno con fill. Es lo que usa el generador antes
    // de excavar las habitaciones.
    void reset(int w, int h, char fill);
    void set(int x, int y, char c);

    // Fuera de rango cuenta como pared solida, asi el raycaster y las
    // colisiones nunca se salen del mapa.
    char at(int x, int y) const;

    // Solo el suelo y la salida se pueden pisar; cualquier otro caracter es
    // pared. Asi agregar una textura nueva no obliga a tocar esta funcion.
    static bool isFloorChar(char c) { return c == ' ' || c == FLOOR || c == EXIT; }
    bool isWall(int x, int y) const { return !isFloorChar(at(x, y)); }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    std::vector<char> cells_;
    int width_ = 0;
    int height_ = 0;
};
