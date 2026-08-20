#pragma once

#include "Fixed.h"

class Framebuffer;
class Maze;
struct Player;

// Un sprite en el mundo. La textura y el fotograma los elige quien lo crea; el
// renderizador solo los dibuja.
struct SpriteInstance {
    fx  x, y;    // posicion en celdas
    int frame;   // fotograma de animacion
};

// Dibuja techo, suelo y paredes, y deja en el z-buffer interno la distancia de
// la pared de cada columna. renderSprites lo consulta despues.
void renderWorld(Framebuffer& fb, const Maze& maze, const Player& player);

// Dibuja los sprites con prueba de profundidad contra el z-buffer, de modo que
// un enemigo detras de una pared no se ve. DEBE llamarse despues de
// renderWorld sobre el mismo frame, o el z-buffer sera el del frame anterior.
void renderSprites(Framebuffer& fb, const Player& player,
                   SpriteInstance* sprites, int count);

void renderMinimap(Framebuffer& fb, const Maze& maze, const Player& player);
