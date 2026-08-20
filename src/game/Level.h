#pragma once

#include <cstdint>

#include "Fixed.h"
#include "Maze.h"

// Donde empieza y termina un piso. Las posiciones van en celdas, centradas,
// listas para asignarselas al jugador.
struct Level {
    fx  startX, startY;
    int exitX, exitY;
    int roomCount;
};

// Genera un piso con habitaciones y pasillos (seccion 13 del PROJECT.md).
//
// La misma seed y el mismo numero de piso producen SIEMPRE el mismo mapa: es lo
// que permite reproducir un bug o compartir una run (seccion 14).
//
// floor arranca en 1 y solo escala el tamano y la cantidad de habitaciones; los
// enemigos y el loot los coloca quien llame, no el generador.
Level generateLevel(Maze& maze, uint32_t seed, int floor);
