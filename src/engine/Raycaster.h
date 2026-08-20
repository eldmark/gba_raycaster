#pragma once

#include "Fixed.h"
#include "Maze.h"

// Resultado de un rayo. Sin nada grafico: el raycaster no sabe que existe una
// pantalla, y por eso sobrevive intacto al port a GBA.
struct Hit {
    fx   perpDist;  // distancia perpendicular al plano de camara, en celdas
    char impact;    // caracter de la pared golpeada
    int  side;      // 0 = cara vertical (cruce en X), 1 = cara horizontal (Y)
    fx   wallX;     // [0,1) donde pego dentro de la pared -> coordenada U
    int  mapX;      // celda golpeada
    int  mapY;
};

// Distancia minima que puede devolver. El llamador divide la altura de pantalla
// entre perpDist, asi que un valor mas chico desbordaria el 16.16. El radio de
// colision del jugador es 0.125, de modo que 1/32 de celda nunca se alcanza en
// la practica.
constexpr fx kMinDist = FX_ONE / 32;

// DDA clasico de Wolfenstein 3D. (posX, posY) y el resultado van en celdas.
//
// perpDist es el t de la ecuacion hit = pos + t * dir, o sea que su unidad la
// define el largo de (dirX, dirY). De ahi sale la correccion de ojo de pez
// gratis: si el llamador arma el rayo como dir + plane * cameraX (el metodo del
// plano de camara de Wolf3D, ver Renderer.cpp) el vector queda sin normalizar
// justo lo necesario para que t sea la distancia PERPENDICULAR al plano.
// Por eso no hay que multiplicar por ningun coseno despues.
//
// Si en cambio se pasa un dir normalizado, t es la distancia euclidiana y las
// paredes se ven curvas.
Hit castRay(const Maze& maze, fx posX, fx posY, fx dirX, fx dirY);
