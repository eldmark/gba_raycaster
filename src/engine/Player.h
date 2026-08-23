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
    bool start = false;  // START en GBA: reiniciar / continuar
    // Pausa y continuar son DOS teclas y no una que alterna: con una sola,
    // el mismo boton que congela la partida es el que la reanuda, y a 12 fps
    // un pulso se lee dos frames seguidos y la pausa parpadea.
    bool pause = false;   // SELECT en GBA, P en el teclado
    bool resume = false;  // START en GBA, C en el teclado

    // Giro analogico, en unidades de angulo ya listas para sumar (65536 = una
    // vuelta). Lo llenan el raton y el stick del mando, que dan una cantidad y
    // no un si/no; el D-PAD y las flechas siguen usando left/right. En GBA se
    // queda en cero y no cuesta nada.
    int32_t turn = 0;

    // Empuje analogico hacia adelante o atras, -FX_ONE..FX_ONE. Cero significa
    // "usa fwd/back", asi que un mando sin stick sigue funcionando.
    fx thrust = 0;
};

// Mueve y rota al jugador respetando las paredes. dt en segundos (16.16).
void updatePlayer(Player& player, const Input& input, const Maze& maze, fx dt);

// Expuesto para poder testearlo aparte.
bool collides(const Maze& maze, fx x, fx y);
