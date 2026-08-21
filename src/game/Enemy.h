#pragma once

#include "Fixed.h"
#include "Maze.h"
#include "Nav.h"

// WARDEN: proceso guardian. Un solo tipo por ahora, como pide la seccion 19 del
// PROJECT.md; los demas se agregan cuando este funcione entero.
struct Enemy {
    enum class State { Idle, Chase, Attack, Dead };
    enum class Kind { Warden, Scout, Boss };

    fx    x, y;
    int   hp;
    State state = State::Idle;
    fx    attackCooldown = 0;  // segundos restantes hasta poder pegar otra vez
    int   frame = 0;           // fotograma de animacion
    fx    frameTimer = 0;
    Kind kind = Kind::Warden;

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
//
// nav es el campo de flujo hacia el jugador, compartido por todos: se usa
// cuando el guardian NO tiene linea de visión, que es cuando ir en linea recta
// lo dejaria empujando una pared.
int updateEnemy(Enemy& e, const Maze& maze, const Nav& nav, fx playerX, fx playerY,
                const EnemyTuning& tuning, fx dt);
