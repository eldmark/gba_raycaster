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
        Transition, // ruta asegurada: breve pantalla entre archivos
        Dead,      // integridad a cero: la run termino
        Cleared,   // se supero el ultimo archivo
    };

    static constexpr int FINAL_FLOOR = 5;  // seccion 26 del PROJECT.md
    static constexpr int START_HP = 100;
    // Un hueco extra sobre los guardianes normales: el nucleo centinela del
    // ultimo archivo ocupa slot propio y no debe robarle sitio a la escolta.
    static constexpr int MAX_ENEMIES = 13;
    // Una pieza por sala, la llave, y el botin de la camara sellada.
    static constexpr int VAULT_LOOT = 3;
    static constexpr int MAX_ITEMS = MAX_LEVEL_ROOMS + 1 + VAULT_LOOT;
    static constexpr int MAX_WORLD_SPRITES = MAX_ENEMIES + MAX_ITEMS + 1;
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
    // Archivo por el que empieza la run. Se elige en la pantalla de titulo con
    // izquierda/derecha; por defecto el primero.
    int      startFloor() const { return startFloor_; }
    int      buffCount() const { return buffCount_; }
    int      ramBuffs() const { return ramBuffs_; }
    int      patchBuffs() const { return patchBuffs_; }
    int      cacheBuffs() const { return cacheBuffs_; }
    // Llave del archivo: abre la camara sellada de este piso. No se arrastra
    // entre pisos, cada archivo tiene la suya.
    bool     hasKey() const { return hasKey_; }
    bool     vaultOpen() const { return vaultOpen_; }

    // --- nucleo centinela -----------------------------------------------------
    // El jefe del ultimo archivo. Mientras siga vivo el protocolo de extraccion
    // no responde, asi que la run no se puede terminar esquivandolo.
    bool bossPresent() const { return bossIndex_ >= 0; }
    bool bossAlive() const;
    int  bossHp() const;
    int  bossHpMax() const { return bossHpMax_; }

    // Puntuacion de la run: bajas, archivos superados y una prima por salir
    // entero. Se calcula, no se acumula, asi no puede desincronizarse.
    int score() const;

    // Segundos jugados de esta run, para el marcador final.
    int elapsedSeconds() const { return fxFloorInt(runTime_); }

    // Cierto durante unas centesimas despues de disparar: lo usa el HUD para
    // el fogonazo.
    bool muzzleFlash() const { return muzzle_ > 0; }
    // Confirmacion visual breve al impactar un guardian.
    bool hitConfirm() const { return hitConfirm_ > 0; }
    const char* pickupNotice() const;
    bool protocolNearby() const;
    // Intensidad restante de la interferencia al recibir dano (0..8).
    int connectionLoss() const;
    int glitchPhase() const { return glitchPhase_; }

    // Cierto cuando el jugador toca el protocolo de extraccion, no solo la
    // celda X que marca la sala final.
    bool onExit() const;

    int enemyCount() const { return enemyCount_; }
    const Enemy& enemy(int i) const { return enemies_[i]; }
    int aliveEnemies() const;

    // Rellena el array de sprites de los enemigos vivos y devuelve cuantos.
    // El renderizador los ordena por profundidad, asi que el orden da igual.
    int buildSprites(SpriteInstance* out, int max) const;
    int buildItemSprites(SpriteInstance* out, int max) const;

private:
    void loadFloor(int floor);
    void spawnEnemies(Random& rng);
    void spawnItems(Random& rng);
    void collectItems();
    void tryUnlockVault();
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
    fx hitConfirm_ = 0;
    fx damageGlitch_ = 0;
    fx transition_ = 0;
    int glitchPhase_ = 0;
    int startFloor_ = 1;
    // START, izquierda y derecha se leen por FLANCO en los menus: por nivel,
    // mantener una tecla pulsada recorreria los cinco archivos en cinco frames.
    bool prevStart_ = false;
    bool prevLeft_ = false, prevRight_ = false;

    Enemy enemies_[MAX_ENEMIES];
    int enemyCount_ = 0;
    // El orden importa: se convierte en indice de sprite sumandole
    // SPR_KIND_ITEM, y ese es el orden en que Textures.h construye los dibujos.
    enum class ItemType { Ram, Patch, Cache, Key, Protocol };
    struct Item { fx x, y; ItemType type; bool collected = false; };
    Item items_[MAX_ITEMS];
    int itemCount_ = 0;
    Item exitProtocol_{};
    int buffCount_ = 0;
    int ramBuffs_ = 0, patchBuffs_ = 0, cacheBuffs_ = 0;
    ItemType lastPickup_ = ItemType::Ram;
    fx pickupNotice_ = 0;
    bool hasKey_ = false;
    bool vaultOpen_ = false;
    int bossIndex_ = -1;   // indice en enemies_, o -1 si el piso no tiene jefe
    int bossHpMax_ = 0;
    Nav nav_;
    // El campo de flujo se recalcula cada varios frames, no cada uno: recorre
    // el mapa entero y los guardianes se mueven despacio.
    int navTimer_ = 0;
    EnemyTuning tuning_{};
    fx gunCooldown_ = 0;
    int gunDamage_ = GUN_DAMAGE;
};
