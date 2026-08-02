#pragma once

class Maze;

struct Player {
    float x, y;  // posicion en celdas
    float a;     // angulo de vista en radianes
    float fov;   // campo de vision en radianes
};

// Entrada abstracta: el motor no sabe de SDL ni de teclas del GBA.
struct Input {
    bool left = false;
    bool right = false;
    bool fwd = false;
    bool back = false;
};

// Mueve y rota al jugador respetando las paredes. dt en segundos.
void updatePlayer(Player& player, const Input& input, const Maze& maze, float dt);

// Expuesto para poder testearlo aparte.
bool collides(const Maze& maze, float x, float y);
