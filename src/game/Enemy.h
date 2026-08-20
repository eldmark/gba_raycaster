#pragma once

#include "Fixed.h"
#include "Maze.h"

// WARDEN: proceso guardian. Un solo tipo por ahora, como pide la seccion 19 del
// PROJECT.md; los demas se agregan cuando este funcione entero.
struct Enemy {
    enum class State { Idle, Chase, Attack, Dead };

    fx    x, y;
    int   hp;
    State state = State::Idle;
    fx    attackCooldown = 0;  // segundos restantes hasta poder pegar otra vez
    int   frame = 0;           // fotograma de animacion
    fx    frameTimer = 0;

    bool alive() const { return state != State::Dead; }
};

// Parametros de comportamiento. Agrupados para que escalar la dificultad por
// piso (seccion 27) sea tocar numeros y no logica.
struct EnemyTuning {
    fx  sightRange;
    fx  attackRange;
    fx  speed;         // celdas por segundo
    fx  attackPeriod;  // segundos entre golpes
    int damage;
    int hp;
};

EnemyTuning tuningForFloor(int floor);

// Avanza un enemigo un frame. Devuelve el dano que le hace al jugador en este
// frame, 0 si ninguno.
int updateEnemy(Enemy& e, const Maze& maze, fx playerX, fx playerY,
                const EnemyTuning& tuning, fx dt);
