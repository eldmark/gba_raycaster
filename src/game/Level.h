#pragma once

#include <cstdint>

#include "Fixed.h"
#include "Maze.h"

// Donde empieza y termina un piso. Las posiciones van en celdas, centradas,
// listas para asignarselas al jugador.
constexpr int MAX_LEVEL_ROOMS = 12;

// Se conserva la geometria de las salas para que el juego pueda colocar loot
// dentro de ellas, en vez de repartirlo a ciegas por pasillos.
struct RoomBounds {
    int x, y, w, h;
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
};

struct Level {
    fx  startX, startY;
    int exitX, exitY;
    int roomCount;
    RoomBounds rooms[MAX_LEVEL_ROOMS];

    // Camara sellada: no se conecta al mapa por un pasillo abierto sino por
    // una unica celda cifrada. Sin la llave del archivo no hay forma de entrar,
    // y el resto del piso se puede terminar sin pisarla.
    bool       hasVault = false;
    RoomBounds vault{};
    int        doorX = 0, doorY = 0;
};

// Genera un piso con habitaciones y pasillos (seccion 13 del PROJECT.md).
//
// La misma seed y el mismo numero de piso producen SIEMPRE el mismo mapa: es lo
// que permite reproducir un bug o compartir una run (seccion 14).
//
// floor arranca en 1 y solo escala el tamano y la cantidad de habitaciones; los
// enemigos y el loot los coloca quien llame, no el generador.
Level generateLevel(Maze& maze, uint32_t seed, int floor);
