#pragma once

#include <cstdint>

#include "Fixed.h"
#include "Level.h"
#include "Maze.h"
#include "Player.h"

// Estado de una run. No sabe nada de SDL ni de libgba: el bucle principal de
// cada plataforma le pasa la entrada y el dt, y le pregunta que dibujar.
class Game {
public:
    enum class State {
        Playing,
        Dead,      // HP a cero: la run termino
        Cleared,   // se supero el ultimo piso
    };

    static constexpr int FINAL_FLOOR = 5;  // seccion 26 del PROJECT.md
    static constexpr int START_HP = 100;

    // seed 0 no es valida para el xorshift; se sustituye por una fija.
    void newRun(uint32_t seed);

    // Avanza un frame. dt en segundos (16.16).
    void update(const Input& input, fx dt);

    const Maze&   maze() const { return maze_; }
    const Player& player() const { return player_; }
    State    state() const { return state_; }
    int      floor() const { return floor_; }
    int      hp() const { return hp_; }
    int      hpMax() const { return hpMax_; }
    uint32_t seed() const { return seed_; }
    int      kills() const { return kills_; }

    // Expuesto para el HUD y para los tests.
    bool onExit() const;

private:
    void loadFloor(int floor);

    Maze   maze_;
    Player player_{};
    Level  level_{};
    State  state_ = State::Playing;
    uint32_t seed_ = 1;
    int floor_ = 1;
    int hp_ = START_HP;
    int hpMax_ = START_HP;
    int kills_ = 0;
};
