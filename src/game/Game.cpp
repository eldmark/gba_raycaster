#include "Game.h"

#include <algorithm>

#include "Raycaster.h"

namespace {
// fov de 60 grados guardado ya como tan(fov/2), que es lo que consume la camara
constexpr fx FOV_TAN_HALF = fxFloat(0.57735027f);

// Radio del enemigo a efectos de recibir un disparo. Como el hitscan compara
// distancias perpendiculares al rayo, esto es el "grosor" del blanco.
constexpr fx HIT_RADIUS = fxFloat(0.45f);

// A menos de esto de la salida o del jugador no aparece ningun enemigo: nacer
// pegado al jugador no es dificultad, es una emboscada injusta.
constexpr fx SPAWN_CLEARANCE = fxInt(5);

// Cuanto dura el fogonazo del disparo.
constexpr fx MUZZLE_TIME = fxFloat(0.07f);

// Puntos por baja y por archivo superado, mas la prima por completar la
// infiltracion entera.
constexpr int SCORE_PER_KILL = 100;
constexpr int SCORE_PER_FLOOR = 250;
constexpr int SCORE_CLEAR_BONUS = 1000;

fx dist2(fx ax, fx ay, fx bx, fx by) {
    fx dx = ax - bx, dy = ay - by;
    return fxMul(dx, dx) + fxMul(dy, dy);
}
}  // namespace

int Game::score() const {
    int s = kills_ * SCORE_PER_KILL + (floor_ - 1) * SCORE_PER_FLOOR;
    if (state_ == State::Cleared) s += SCORE_PER_FLOOR + SCORE_CLEAR_BONUS;
    return s;
}

void Game::newRun(uint32_t seed) {
    seed_ = seed ? seed : 1u;
    // la siguiente run parte de otra semilla, o repetiria nivel
    nextSeed_ = seed_ * 1664525u + 1013904223u;
    runTime_ = 0;
    muzzle_ = 0;
    hpMax_ = START_HP;
    hp_ = hpMax_;
    kills_ = 0;
    state_ = State::Playing;
    loadFloor(1);
}

void Game::loadFloor(int floor) {
    floor_ = floor;
    level_ = generateLevel(maze_, seed_, floor);
    player_.x = level_.startX;
    player_.y = level_.startY;
    player_.fov = FOV_TAN_HALF;
    // mirando al este; da igual cual sea, pero fijarlo mantiene la run
    // reproducible a partir de la seed
    player_.a = 0;
    gunCooldown_ = 0;
    tuning_ = tuningForFloor(floor);

    // La misma seed y el mismo piso deben colocar los mismos enemigos, no solo
    // el mismo mapa, o una run no seria reproducible del todo.
    Random rng(seed_ * 2246822519u + uint32_t(floor) * 3266489917u);
    spawnEnemies(rng);
}

void Game::spawnEnemies(Random& rng) {
    enemyCount_ = 0;
    // Mas enemigos segun el piso, con tope (seccion 27).
    const int wanted = std::min(MAX_ENEMIES, 3 + floor_ * 2);
    const fx clear2 = fxMul(SPAWN_CLEARANCE, SPAWN_CLEARANCE);

    for (int attempt = 0; attempt < wanted * 40 && enemyCount_ < wanted; ++attempt) {
        int cx = rng.range(1, maze_.width() - 2);
        int cy = rng.range(1, maze_.height() - 2);
        if (maze_.isWall(cx, cy)) continue;

        fx px = fxInt(cx) + FX_ONE / 2;
        fx py = fxInt(cy) + FX_ONE / 2;
        if (dist2(px, py, player_.x, player_.y) < clear2) continue;

        Enemy e{};
        e.x = px;
        e.y = py;
        e.hp = tuning_.hp;
        e.state = Enemy::State::Idle;
        e.frame = rng.range(0, 1);  // desfasar el latido entre enemigos
        enemies_[enemyCount_++] = e;
    }
}

bool Game::onExit() const {
    return maze_.at(fxFloorInt(player_.x), fxFloorInt(player_.y)) == Maze::EXIT;
}

int Game::aliveEnemies() const {
    int n = 0;
    for (int i = 0; i < enemyCount_; ++i) {
        if (enemies_[i].alive()) ++n;
    }
    return n;
}

int Game::buildSprites(SpriteInstance* out, int max) const {
    int n = 0;
    for (int i = 0; i < enemyCount_ && n < max; ++i) {
        if (!enemies_[i].alive()) continue;
        out[n++] = SpriteInstance{enemies_[i].x, enemies_[i].y, enemies_[i].frame};
    }
    return n;
}

void Game::fire() {
    // Hitscan (seccion 17): un rayo desde la camara. Si hay pared antes que el
    // enemigo, el tiro se pierde; asi no hace falta simular proyectiles.
    const fx dirX = fxCos(player_.a);
    const fx dirY = fxSin(player_.a);
    const Hit wall = castRay(maze_, player_.x, player_.y, dirX, dirY);

    int best = -1;
    fx bestDist = GUN_RANGE;

    for (int i = 0; i < enemyCount_; ++i) {
        Enemy& e = enemies_[i];
        if (!e.alive()) continue;

        const fx relX = e.x - player_.x;
        const fx relY = e.y - player_.y;

        // proyeccion sobre el rayo: cuan lejos por delante esta el enemigo
        const fx along = fxMul(relX, dirX) + fxMul(relY, dirY);
        if (along <= 0 || along > bestDist) continue;      // detras, o mas lejos
        if (along > wall.perpDist) continue;               // la pared lo tapa

        // distancia perpendicular al rayo: cuanto se desvia del centro de la
        // mira. Es lo que decide si el tiro entra en el blanco.
        const fx offX = relX - fxMul(along, dirX);
        const fx offY = relY - fxMul(along, dirY);
        const fx off2 = fxMul(offX, offX) + fxMul(offY, offY);
        if (off2 > fxMul(HIT_RADIUS, HIT_RADIUS)) continue;

        // el mas cercano de los que estan en la mira se lleva el tiro
        best = i;
        bestDist = along;
    }

    if (best < 0) return;

    Enemy& hitEnemy = enemies_[best];
    hitEnemy.hp -= GUN_DAMAGE;
    if (hitEnemy.hp <= 0) {
        hitEnemy.state = Enemy::State::Dead;
        ++kills_;
    }
}

void Game::update(const Input& input, fx dt) {
    // START por flanco: si se leyera por nivel, mantenerlo pulsado atravesaria
    // las tres pantallas en tres frames
    const bool startPressed = input.start && !prevStart_;
    prevStart_ = input.start;

    switch (state_) {
        case State::Title:
            if (startPressed) state_ = State::Rules;
            return;
        case State::Rules:
            if (startPressed) newRun(nextSeed_);
            return;
        case State::Dead:
        case State::Cleared:
            if (startPressed) state_ = State::Title;
            return;
        case State::Playing:
            break;
    }

    runTime_ += dt;
    if (muzzle_ > 0) muzzle_ -= dt;

    updatePlayer(player_, input, maze_, dt);

    if (gunCooldown_ > 0) gunCooldown_ -= dt;
    if (input.fire && gunCooldown_ <= 0) {
        gunCooldown_ = GUN_PERIOD;
        muzzle_ = MUZZLE_TIME;
        fire();
    }

    for (int i = 0; i < enemyCount_; ++i) {
        hp_ -= updateEnemy(enemies_[i], maze_, player_.x, player_.y, tuning_, dt);
    }

    if (hp_ <= 0) {
        hp_ = 0;
        state_ = State::Dead;
        return;
    }

    if (onExit()) {
        if (floor_ >= FINAL_FLOOR) {
            state_ = State::Cleared;
        } else {
            hp_ = std::min(hpMax_, hp_ + FLOOR_HEAL);
            loadFloor(floor_ + 1);
        }
    }
}
