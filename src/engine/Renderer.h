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
    int kind = 0; // 0 WARDEN; los demas son objetos recogibles
};

// Fila del horizonte para un jugador y una altura de pantalla dados. El HUD la
// usa para poner la mira donde de verdad se esta apuntando, asi que vive aqui y
// no duplicada en Hud.cpp.
int horizonY(int screenH, const Player& player);

// Dibuja techo, suelo y paredes, y deja en el z-buffer interno la distancia de
// la pared de cada columna. renderSprites lo consulta despues.
void renderWorld(Framebuffer& fb, const Maze& maze, const Player& player);

// Dibuja los sprites con prueba de profundidad contra el z-buffer, de modo que
// un enemigo detras de una pared no se ve. DEBE llamarse despues de
// renderWorld sobre el mismo frame, o el z-buffer sera el del frame anterior.
void renderSprites(Framebuffer& fb, const Player& player,
                   SpriteInstance* sprites, int count);

// Distorsiona brevemente el framebuffer tras recibir dano: lineas que se
// desalinean y paquetes perdidos, como una conexion que esta cayendose.
void renderConnectionLoss(Framebuffer& fb, int strength, int phase);

void renderMinimap(Framebuffer& fb, const Maze& maze, const Player& player);
// Marcadores del minimapa: pickups/protocolo y solo enemigos con linea de
// vision, para informar sin convertir el mapa en un radar total.
void renderMinimapEntities(Framebuffer& fb, const Maze& maze, const Player& player,
                           const SpriteInstance* sprites, int count);
