#pragma once

#include "Fixed.h"

class Maze;

struct Player {
    fx    x, y;  // posicion en celdas
    angle a;     // angulo de vista; 65536 = una vuelta, se envuelve solo
    fx    fov;   // campo de vision, guardado ya como tan(fov/2)
};

// Entrada abstracta: el motor no sabe de SDL ni de teclas del GBA.
struct Input {
    bool left = false;
    bool right = false;
    bool fwd = false;
    bool back = false;
    bool fire = false;   // A en GBA: disparar
    bool start = false;  // START en GBA: reiniciar / pausar
};

// Mueve y rota al jugador respetando las paredes. dt en segundos (16.16).
void updatePlayer(Player& player, const Input& input, const Maze& maze, fx dt);

// Expuesto para poder testearlo aparte.
bool collides(const Maze& maze, fx x, fx y);
