#include "Game.h"

#include <algorithm>

#include "Raycaster.h"
#include "Textures.h"  // SPR_KIND_*

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
constexpr fx HIT_CONFIRM_TIME = fxFloat(0.14f);
constexpr fx DAMAGE_GLITCH_TIME = fxFloat(0.65f);
constexpr fx PICKUP_RADIUS = fxFloat(0.55f);
constexpr fx TRANSITION_TIME = fxFloat(1.6f);
constexpr fx PICKUP_NOTICE_TIME = fxFloat(1.3f);
constexpr fx PROTOCOL_NOTICE_RANGE = fxInt(4);

// Puntos por baja y por archivo superado, mas la prima por completar la
// infiltracion entera.
constexpr int SCORE_PER_KILL = 100;
constexpr int SCORE_PER_FLOOR = 250;
constexpr int SCORE_CLEAR_BONUS = 1000;
// El nucleo centinela vale mas que una baja normal porque es obligatorio; la
// camara sellada vale menos porque es opcional, pero se paga por cada pieza.
constexpr int SCORE_BOSS_BONUS = 750;

// A que distancia se descifra la puerta al llevar la llave. Algo mas que el
// radio de recogida: la puerta es pared, asi que no se puede pisar su celda.
constexpr fx UNLOCK_RADIUS = fxFloat(1.6f);

// Cada cuantos frames se rehace el campo de flujo. A 60 FPS son cuatro veces
// por segundo; un guardian rapido recorre media celda en ese tiempo.
constexpr int NAV_PERIOD = 15;

fx dist2(fx ax, fx ay, fx bx, fx by) {
    fx dx = ax - bx, dy = ay - by;
    return fxMul(dx, dx) + fxMul(dy, dy);
}
}  // namespace

int Game::score() const {
    // Los archivos se cuentan desde donde arranco la run, no desde el uno:
    // empezar en el 5 con el selector no puede regalar cuatro pisos de puntos.
    int s = kills_ * SCORE_PER_KILL + (floor_ - startFloor_) * SCORE_PER_FLOOR;
    if (state_ == State::Cleared) s += SCORE_PER_FLOOR + SCORE_CLEAR_BONUS;
    if (bossIndex_ >= 0 && !bossAlive()) s += SCORE_BOSS_BONUS;
    return s;
}

void Game::newRun(uint32_t seed) {
    seed_ = seed ? seed : 1u;
    // la siguiente run parte de otra semilla, o repetiria nivel
    nextSeed_ = seed_ * 1664525u + 1013904223u;
    runTime_ = 0;
    muzzle_ = 0;
    hitConfirm_ = 0;
    damageGlitch_ = 0;
    transition_ = 0;
    glitchPhase_ = 0;
    hpMax_ = START_HP;
    hp_ = hpMax_;
    kills_ = 0;
    buffCount_ = 0;
    ramBuffs_ = patchBuffs_ = cacheBuffs_ = 0;
    pickupNotice_ = 0;
    gunDamage_ = GUN_DAMAGE;
    hasKey_ = false;
    vaultOpen_ = false;
    state_ = State::Playing;
    loadFloor(startFloor_);
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
    // La llave es de este archivo: llevarsela al siguiente convertiria las
    // camaras posteriores en salas abiertas.
    hasKey_ = false;
    vaultOpen_ = !level_.hasVault;
    bossIndex_ = -1;
    bossHpMax_ = 0;
    // El ascensor de piso ya no es una celda invisible: el protocolo queda en
    // el centro de la sala de extraccion y se activa al tocar su sprite.
    exitProtocol_ = Item{fxInt(level_.exitX) + FX_ONE / 2,
                         fxInt(level_.exitY) + FX_ONE / 2,
                         ItemType::Protocol};

    // La misma seed y el mismo piso deben colocar los mismos enemigos, no solo
    // el mismo mapa, o una run no seria reproducible del todo.
    Random rng(seed_ * 2246822519u + uint32_t(floor) * 3266489917u);
    spawnEnemies(rng);
    spawnItems(rng);

    nav_.rebuild(maze_, fxFloorInt(player_.x), fxFloorInt(player_.y));
    navTimer_ = 0;
}

void Game::spawnItems(Random& rng) {
    itemCount_ = 0;
    auto place = [&](const RoomBounds& room, ItemType type) {
        if (itemCount_ >= MAX_ITEMS) return;
        int x = rng.range(room.x + 1, room.x + room.w - 2);
        int y = rng.range(room.y + 1, room.y + room.h - 2);
        items_[itemCount_++] = Item{fxInt(x) + FX_ONE / 2, fxInt(y) + FX_ONE / 2, type};
    };

    // Una pieza por sala visitable, excepto inicio y extraccion: asi los
    // objetos aparecen en habitaciones y dan una razon para explorarlas.
    for (int r = 1; r + 1 < level_.roomCount; ++r) {
        place(level_.rooms[r], static_cast<ItemType>(rng.range(0, 2)));
    }

    if (!level_.hasVault) return;

    // La llave nunca cae en la sala de inicio: encontrarla sin moverse dejaria
    // la camara sin el rodeo que la justifica.
    place(level_.rooms[level_.roomCount > 1 ? rng.range(1, level_.roomCount - 1) : 0],
          ItemType::Key);

    // Botin de la camara: tres piezas juntas, que es lo que hace que valga la
    // pena el rodeo de buscar la llave.
    for (int i = 0; i < VAULT_LOOT; ++i) {
        place(level_.vault, static_cast<ItemType>(rng.range(0, 2)));
    }
}

bool Game::bossAlive() const {
    return bossIndex_ >= 0 && enemies_[bossIndex_].alive();
}

int Game::bossHp() const {
    return bossIndex_ >= 0 ? enemies_[bossIndex_].hp : 0;
}

void Game::tryUnlockVault() {
    if (vaultOpen_ || !hasKey_ || !level_.hasVault) return;

    const RoomBounds& v = level_.vault;
    const fx unlock2 = fxMul(UNLOCK_RADIUS, UNLOCK_RADIUS);
    bool near = false;
    for (int y = v.y - 1; y <= v.y + v.h && !near; ++y) {
        for (int x = v.x - 1; x <= v.x + v.w && !near; ++x) {
            if (maze_.at(x, y) != Maze::DOOR) continue;
            near = dist2(fxInt(x) + FX_ONE / 2, fxInt(y) + FX_ONE / 2,
                         player_.x, player_.y) <= unlock2;
        }
    }
    if (!near) return;

    // El cifrado es de la camara entera, no de una casilla: se abren todas las
    // celdas de puerta a la vez. Y pasan a ser suelo de verdad, no una
    // excepcion en la colision, asi que el raycaster, el pathfinding y el
    // jugador las ven abiertas al mismo tiempo.
    for (int y = v.y - 1; y <= v.y + v.h; ++y) {
        for (int x = v.x - 1; x <= v.x + v.w; ++x) {
            if (maze_.at(x, y) == Maze::DOOR) maze_.set(x, y, Maze::FLOOR);
        }
    }
    vaultOpen_ = true;
    lastPickup_ = ItemType::Key;
    pickupNotice_ = PICKUP_NOTICE_TIME;
    // El campo de flujo se rehace ya: si esperara al turno normal, un guardian
    // seguiria dando vueltas contra una puerta que ya no existe.
    nav_.rebuild(maze_, fxFloorInt(player_.x), fxFloorInt(player_.y));
    navTimer_ = NAV_PERIOD;
}

void Game::spawnEnemies(Random& rng) {
    enemyCount_ = 0;

    // --- nucleo centinela -----------------------------------------------------
    // Solo en el ultimo archivo, y plantado en la sala de extraccion: es lo que
    // convierte el final en un combate en vez de una carrera hasta la salida.
    if (floor_ >= FINAL_FLOOR) {
        Enemy boss{};
        boss.kind = Enemy::Kind::Boss;
        boss.x = fxInt(level_.exitX) + FX_ONE / 2;
        // Una celda por delante del protocolo: se interpone entre el jugador y
        // la salida en vez de aparecer encima de ella.
        boss.y = fxInt(level_.exitY - 1) + FX_ONE / 2;
        bossHpMax_ = tuning_.hp * 6;
        boss.hp = bossHpMax_;
        boss.state = Enemy::State::Idle;
        bossIndex_ = enemyCount_;
        enemies_[enemyCount_++] = boss;
    }

    // Mas enemigos segun el piso, con tope (seccion 27).
    const int wanted = std::min(MAX_ENEMIES, enemyCount_ + 3 + floor_ * 2);
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
        // SCOUT: ligero y frágil. A partir del piso 2 introduce presión sin
        // convertir todos los encuentros en esponjas de daño.
        e.kind = (floor_ >= 2 && rng.chance(32)) ? Enemy::Kind::Scout
                                                  : Enemy::Kind::Warden;
        if (e.kind == Enemy::Kind::Scout) e.hp = std::max(1, tuning_.hp - 8);
        enemies_[enemyCount_++] = e;
    }
}

bool Game::onExit() const {
    // Con el nucleo centinela en pie el protocolo no responde: si no, el ultimo
    // archivo se ganaria corriendo en linea recta y esquivando al jefe.
    if (bossAlive()) return false;
    const fx radius2 = fxMul(PICKUP_RADIUS, PICKUP_RADIUS);
    return dist2(exitProtocol_.x, exitProtocol_.y, player_.x, player_.y) <= radius2;
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
        const int kind = enemies_[i].kind == Enemy::Kind::Scout ? SPR_KIND_SCOUT
                       : enemies_[i].kind == Enemy::Kind::Boss  ? SPR_KIND_BOSS
                                                                : SPR_KIND_WARDEN;
        out[n++] = SpriteInstance{enemies_[i].x, enemies_[i].y, enemies_[i].frame, kind};
    }
    return n;
}

const char* Game::pickupNotice() const {
    if (pickupNotice_ <= 0) return nullptr;
    switch (lastPickup_) {
        case ItemType::Ram: return "+ RAM: INTEGRIDAD MAXIMA +10";
        case ItemType::Patch: return "+ PATCH: INTEGRIDAD +30";
        case ItemType::Cache: return "+ CACHE: DANIO +4";
        case ItemType::Key:
            return vaultOpen_ ? "CIFRADO ROTO: CAMARA ABIERTA"
                              : "+ LLAVE DE ARCHIVO";
        case ItemType::Protocol: return nullptr;
    }
    return nullptr;
}

bool Game::protocolNearby() const {
    return dist2(exitProtocol_.x, exitProtocol_.y, player_.x, player_.y) <=
           fxMul(PROTOCOL_NOTICE_RANGE, PROTOCOL_NOTICE_RANGE);
}

int Game::buildItemSprites(SpriteInstance* out, int max) const {
    int n = 0;
    for (int i = 0; i < itemCount_ && n < max; ++i) {
        if (items_[i].collected) continue;
        out[n++] = SpriteInstance{items_[i].x, items_[i].y, 0,
                                  SPR_KIND_ITEM + int(items_[i].type)};
    }
    if (n < max) {
        out[n++] = SpriteInstance{exitProtocol_.x, exitProtocol_.y, 0,
                                  SPR_KIND_ITEM + int(ItemType::Protocol)};
    }
    return n;
}

int Game::connectionLoss() const {
    if (damageGlitch_ <= 0) return 0;
    return std::min(8, 1 + int((damageGlitch_ * 8) >> FX_BITS));
}

void Game::collectItems() {
    const fx radius2 = fxMul(PICKUP_RADIUS, PICKUP_RADIUS);
    for (int i = 0; i < itemCount_; ++i) {
        Item& item = items_[i];
        if (item.collected || dist2(item.x, item.y, player_.x, player_.y) > radius2) continue;
        item.collected = true;
        ++buffCount_;
        lastPickup_ = item.type;
        pickupNotice_ = PICKUP_NOTICE_TIME;
        switch (item.type) {
            case ItemType::Ram:
                ++ramBuffs_;
                hpMax_ += 10;
                hp_ = std::min(hpMax_, hp_ + 20);
                break;
            case ItemType::Patch:
                ++patchBuffs_;
                hp_ = std::min(hpMax_, hp_ + 30);
                break;
            case ItemType::Cache:
                ++cacheBuffs_;
                gunDamage_ += 4;
                break;
            case ItemType::Key:
                hasKey_ = true;
                --buffCount_;  // la llave no es una mejora, es un permiso
                break;
            case ItemType::Protocol:
                break;  // el protocolo se gestiona por onExit()
        }
    }
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
    hitConfirm_ = HIT_CONFIRM_TIME;
    hitEnemy.hp -= gunDamage_;
    if (hitEnemy.hp <= 0) {
        hitEnemy.state = Enemy::State::Dead;
        ++kills_;
    }
}

void Game::update(const Input& input, fx dt) {
    // START por flanco: si se leyera por nivel, mantenerlo pulsado atravesaria
    // las tres pantallas en tres frames
    const bool startPressed = input.start && !prevStart_;
    const bool leftPressed = input.left && !prevLeft_;
    const bool rightPressed = input.right && !prevRight_;
    prevStart_ = input.start;
    prevLeft_ = input.left;
    prevRight_ = input.right;

    switch (state_) {
        case State::Title:
            // Selector de archivo. Se puede entrar por cualquiera de los cinco;
            // el marcador descuenta los que se saltaron.
            if (leftPressed && startFloor_ > 1) --startFloor_;
            if (rightPressed && startFloor_ < FINAL_FLOOR) ++startFloor_;
            if (startPressed) state_ = State::Rules;
            return;
        case State::Rules:
            if (startPressed) newRun(nextSeed_);
            return;
        case State::Transition:
            transition_ -= dt;
            if (transition_ <= 0) state_ = State::Playing;
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
    if (hitConfirm_ > 0) hitConfirm_ -= dt;
    if (pickupNotice_ > 0) pickupNotice_ -= dt;
    if (damageGlitch_ > 0) damageGlitch_ -= dt;
    ++glitchPhase_;

    updatePlayer(player_, input, maze_, dt);
    collectItems();
    tryUnlockVault();

    if (gunCooldown_ > 0) gunCooldown_ -= dt;
    if (input.fire && gunCooldown_ <= 0) {
        gunCooldown_ = GUN_PERIOD;
        muzzle_ = MUZZLE_TIME;
        fire();
    }

    if (--navTimer_ <= 0) {
        navTimer_ = NAV_PERIOD;
        nav_.rebuild(maze_, fxFloorInt(player_.x), fxFloorInt(player_.y));
    }

    int damage = 0;
    for (int i = 0; i < enemyCount_; ++i) {
        // Un cadaver no necesita que se le copie el tuning ni se le mire la
        // clase: updateEnemy lo iba a descartar en la primera linea.
        if (!enemies_[i].alive()) continue;
        EnemyTuning current = tuning_;
        if (enemies_[i].kind == Enemy::Kind::Scout) {
            current.speed += fxFloat(0.55f);
            current.damage = std::max(1, current.damage - 2);
            current.attackPeriod += fxFloat(0.15f);
        } else if (enemies_[i].kind == Enemy::Kind::Boss) {
            // Lento y de vista larga: se le puede ganar terreno retrocediendo,
            // pero pega el triple y no deja de seguirte por toda la sala.
            current.speed = fxFloat(1.15f);
            current.sightRange = fxInt(16);
            current.attackRange = fxFloat(1.8f);
            current.damage += 6;
            current.attackPeriod = fxFloat(1.4f);
        }
        damage += updateEnemy(enemies_[i], maze_, nav_, player_.x, player_.y, current, dt);
    }
    if (damage > 0) {
        hp_ -= damage;
        damageGlitch_ = DAMAGE_GLITCH_TIME;
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
            transition_ = TRANSITION_TIME;
            state_ = State::Transition;
        }
    }
}
