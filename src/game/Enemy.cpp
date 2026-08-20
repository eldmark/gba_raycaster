#include "Enemy.h"

namespace {

// Cuadrado de la distancia: evita una raiz por enemigo y por frame. Todas las
// comparaciones de rango se hacen tambien al cuadrado.
fx dist2(fx ax, fx ay, fx bx, fx by) {
    fx dx = ax - bx, dy = ay - by;
    return fxMul(dx, dx) + fxMul(dy, dy);
}

constexpr fx RADIUS = fxFloat(0.2f);

bool blocked(const Maze& maze, fx x, fx y) {
    const fx off[4][2] = {{-RADIUS, 0}, {RADIUS, 0}, {0, -RADIUS}, {0, RADIUS}};
    for (const auto& o : off) {
        if (maze.isWall(fxFloorInt(x + o[0]), fxFloorInt(y + o[1]))) return true;
    }
    return false;
}

// Frecuencia del latido del sprite, en segundos por fotograma.
constexpr fx FRAME_PERIOD = fxFloat(0.35f);

}  // namespace

EnemyTuning tuningForFloor(int floor) {
    // Escalado lineal y suave: el piso 5 pega el doble que el 1 y aguanta algo
    // mas, pero no tanto como para que el arma deje de servir.
    return EnemyTuning{
        fxInt(8),                            // vista
        fxFloat(1.2f),                       // alcance del golpe
        fxFloat(1.4f) + fxFloat(0.15f) * (floor - 1),
        fxFloat(1.2f),                       // periodo de golpe
        6 + 2 * (floor - 1),                 // dano
        20 + 6 * (floor - 1),                // vida
    };
}

int updateEnemy(Enemy& e, const Maze& maze, fx playerX, fx playerY,
                const EnemyTuning& tuning, fx dt) {
    if (!e.alive()) return 0;

    // El latido corre siempre, tambien en Idle: un guardian quieto pero
    // parpadeando se lee como dormido y no como decorado.
    e.frameTimer += dt;
    while (e.frameTimer >= FRAME_PERIOD) {
        e.frameTimer -= FRAME_PERIOD;
        e.frame ^= 1;
    }

    if (e.attackCooldown > 0) e.attackCooldown -= dt;

    const fx d2 = dist2(e.x, e.y, playerX, playerY);

    if (e.state == Enemy::State::Idle) {
        if (d2 > fxMul(tuning.sightRange, tuning.sightRange)) return 0;
        e.state = Enemy::State::Chase;
    }

    if (d2 <= fxMul(tuning.attackRange, tuning.attackRange)) {
        e.state = Enemy::State::Attack;
        if (e.attackCooldown <= 0) {
            e.attackCooldown = tuning.attackPeriod;
            return tuning.damage;
        }
        return 0;
    }

    e.state = Enemy::State::Chase;

    // Persecucion directa, sin pathfinding. La seccion 20 lo pide asi: BFS solo
    // si se demuestra que se atascan.
    fx dx = playerX - e.x;
    fx dy = playerY - e.y;
    // normalizar con la aproximacion octogonal: |v| ~ max + min/2. Evita una
    // raiz cuadrada y se equivoca como mucho un 12%, que en la velocidad de un
    // enemigo no se nota.
    fx ax = fxAbs(dx), ay = fxAbs(dy);
    fx len = (ax > ay) ? (ax + (ay >> 1)) : (ay + (ax >> 1));
    if (len <= 0) return 0;

    fx step = fxMul(tuning.speed, dt);
    fx mx = fxMul(fxDiv(dx, len), step);
    fx my = fxMul(fxDiv(dy, len), step);

    // cada eje por separado, igual que el jugador: permite deslizarse por la
    // pared en vez de quedarse clavado contra una esquina
    if (!blocked(maze, e.x + mx, e.y)) e.x += mx;
    if (!blocked(maze, e.x, e.y + my)) e.y += my;

    return 0;
}
