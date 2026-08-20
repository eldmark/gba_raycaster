#pragma once

#include <cstdint>

#include "Fixed.h"
#include "Level.h"
#include "Maze.h"
#include "Nav.h"
#include "Random.h"
#include "Enemy.h"
#include "Player.h"
#include "Renderer.h"

// Estado de una run. No sabe nada de SDL ni de libgba: el bucle principal de
// cada plataforma le pasa la entrada y el dt, y le pregunta que dibujar.
class Game {
public:
    enum class State {
        Title,     // pantalla de bienvenida
        Rules,     // reglas, antes de conectar
        Playing,
        Dead,      // integridad a cero: la run termino
        Cleared,   // se supero el ultimo archivo
    };

    static constexpr int FINAL_FLOOR = 5;  // seccion 26 del PROJECT.md
    static constexpr int START_HP = 100;
    static constexpr int MAX_ENEMIES = 12;
    // Integridad que se recupera al enlazar con el siguiente archivo. Sin esto
    // la vida solo baja y una run de cinco pisos no se puede terminar.
    static constexpr int FLOOR_HEAL = 30;

    // PACKET GUN: la unica arma del MVP (seccion 18).
    static constexpr int   GUN_DAMAGE = 12;
    static constexpr fx    GUN_PERIOD = fxFloat(0.35f);  // segundos entre tiros
    static constexpr fx    GUN_RANGE = fxInt(12);

    // Semilla de la SIGUIENTE run. Cada run que empieza la hace avanzar, asi
    // que dos partidas seguidas no repiten nivel.
    void setSeed(uint32_t seed) { nextSeed_ = seed ? seed : 1u; }

    // Empieza a jugar ya, saltandose las pantallas. Es la puerta que usan los
    // tests; el juego normal entra por Title.
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

    // Puntuacion de la run: bajas, archivos superados y una prima por salir
    // entero. Se calcula, no se acumula, asi no puede desincronizarse.
    int score() const;

    // Segundos jugados de esta run, para el marcador final.
    int elapsedSeconds() const { return fxFloorInt(runTime_); }

    // Cierto durante unas centesimas despues de disparar: lo usa el HUD para
    // el fogonazo.
    bool muzzleFlash() const { return muzzle_ > 0; }

    // Expuesto para el HUD y para los tests.
    bool onExit() const;

    int enemyCount() const { return enemyCount_; }
    const Enemy& enemy(int i) const { return enemies_[i]; }
    int aliveEnemies() const;

    // Rellena el array de sprites de los enemigos vivos y devuelve cuantos.
    // El renderizador los ordena por profundidad, asi que el orden da igual.
    int buildSprites(SpriteInstance* out, int max) const;

private:
    void loadFloor(int floor);
    void spawnEnemies(Random& rng);
    void fire();

    Maze   maze_;
    Player player_{};
    Level  level_{};
    State  state_ = State::Title;
    uint32_t seed_ = 1;
    int floor_ = 1;
    int hp_ = START_HP;
    int hpMax_ = START_HP;
    int kills_ = 0;

    uint32_t nextSeed_ = 1;
    fx runTime_ = 0;
    fx muzzle_ = 0;
    bool prevStart_ = false;  // START se lee por flanco, no por nivel

    Enemy enemies_[MAX_ENEMIES];
    int enemyCount_ = 0;
    Nav nav_;
    // El campo de flujo se recalcula cada varios frames, no cada uno: recorre
    // el mapa entero y los guardianes se mueven despacio.
    int navTimer_ = 0;
    EnemyTuning tuning_{};
    fx gunCooldown_ = 0;
};
