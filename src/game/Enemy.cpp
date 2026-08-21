#include "Enemy.h"

#include "Raycaster.h"

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

// Hay pared entre el guardian y el jugador?
bool canSee(const Maze& maze, fx ex, fx ey, fx px, fx py) {
    const fx dx = px - ex, dy = py - ey;
    if (dx == 0 && dy == 0) return true;
    // castRay devuelve la distancia en unidades del vector que se le pasa, asi
    // que 1.0 es exactamente la posicion del jugador
    return castRay(maze, ex, ey, dx, dy).perpDist > FX_ONE;
}

}  // namespace

EnemyTuning tuningForFloor(int floor) {
    // Escalado lineal. El dano se subio de 6+2/piso a 10+4/piso: con los
    // pickups de RAM y PATCH repartidos por las salas, la version anterior
    // dejaba llegar al ultimo archivo casi sin gastar integridad y los
    // guardianes eran un tramite en vez de una amenaza. Ahora un descuido de
    // tres golpes en el piso 5 cuesta mas de la mitad de la barra.
    return EnemyTuning{
        fxInt(9),                            // vista
        fxFloat(1.3f),                       // alcance del golpe
        fxFloat(1.4f) + fxFloat(0.15f) * (floor - 1),
        fxFloat(1.0f),                       // periodo de golpe
        10 + 4 * (floor - 1),                // dano
        20 + 6 * (floor - 1),                // vida
    };
}

int updateEnemy(Enemy& e, const Maze& maze, const Nav& nav, fx playerX, fx playerY,
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

    // La comprobacion de rango va ANTES del rayo, no despues. Un guardian
    // dormido y fuera de vista descartaba el resultado sin leerlo: cuatro de
    // cada cinco rayos que lanzaba la IA no los miraba nadie.
    if (e.state == Enemy::State::Idle &&
        d2 > fxMul(tuning.sightRange, tuning.sightRange)) {
        return 0;
    }

    const bool sees = canSee(maze, e.x, e.y, playerX, playerY);

    if (e.state == Enemy::State::Idle) {
        // ver, no solo estar cerca: antes despertaban a traves de los muros, y
        // ademas de ser injusto los dejaba persiguiendo algo inalcanzable
        if (!sees) return 0;
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

    // La ruta del campo de flujo, si existe. Se apunta al centro de la celda
    // siguiente, no a su esquina, para no rozar el muro al enfilar un pasillo.
    fx flowX = 0, flowY = 0;
    int sx, sy;
    const bool hasRoute = nav.step(fxFloorInt(e.x), fxFloorInt(e.y), sx, sy);
    if (hasRoute) {
        flowX = fxInt(fxFloorInt(e.x) + sx) + FX_ONE / 2 - e.x;
        flowY = fxInt(fxFloorInt(e.y) + sy) + FX_ONE / 2 - e.y;
    }

    if (!sees && !hasRoute) {
        // sin ruta y sin verlo: el jugador esta incomunicado de este guardian
        e.state = Enemy::State::Idle;
        return 0;
    }

    // Convierte una direccion en un paso por eje. La normalizacion usa la
    // aproximacion octogonal |v| ~ max + min/2: evita una raiz cuadrada y se
    // equivoca como mucho un 12%, que en la velocidad de un enemigo no se nota.
    // Una sola division, repartida a los dos ejes: en ARMv4T cada division que
    // se borra son ~400 ciclos.
    const fx step = fxMul(tuning.speed, dt);
    auto stepAlong = [&](fx dx, fx dy, fx& mx, fx& my) {
        const fx ax = fxAbs(dx), ay = fxAbs(dy);
        const fx len = (ax > ay) ? (ax + (ay >> 1)) : (ay + (ax >> 1));
        if (len <= 0) {
            mx = my = 0;
            return false;
        }
        const fx k = fxDiv(step, len);
        mx = fxMul(dx, k);
        my = fxMul(dy, k);
        return true;
    };

    // Con el jugador a la vista se va derecho, que se mueve mas natural en una
    // sala abierta. Sin verlo se sigue el campo de flujo, o el guardian se
    // queda empujando la esquina que tiene delante.
    fx mx = 0, my = 0;
    bool moving = sees ? stepAlong(playerX - e.x, playerY - e.y, mx, my)
                       : stepAlong(flowX, flowY, mx, my);
    if (!moving) return 0;

    bool freeX = !blocked(maze, e.x + mx, e.y);
    bool freeY = !blocked(maze, e.x, e.y + my);

    // Ir derecho no basta cuando el muro estorba. Con el jugador en linea casi
    // recta -por ejemplo al fondo de un pasillo- la componente lateral del
    // rumbo directo es diminuta, asi que un guardian con el hombro contra una
    // esquina tardaba SEGUNDOS en librarla: medido, 0.0013 celdas por frame
    // durante cinco segundos. El campo de flujo apunta al centro de la celda
    // siguiente y rodea la esquina de inmediato, asi que en cuanto un eje se
    // bloquea se cambia a el para este frame.
    if (sees && hasRoute && !(freeX && freeY)) {
        fx fx_ = 0, fy_ = 0;
        if (stepAlong(flowX, flowY, fx_, fy_)) {
            const bool altX = !blocked(maze, e.x + fx_, e.y);
            const bool altY = !blocked(maze, e.x, e.y + fy_);
            // Basta con que el campo de flujo no libere MENOS ejes que el
            // rumbo directo. Con "estrictamente mas" no arreglaba nada: en el
            // caso medido ambos dejaban libre solo el eje X, pero el rumbo
            // directo apenas se desplazaba en el y el campo de flujo apunta de
            // lleno hacia el centro de la celda contigua.
            if (int(altX) + int(altY) >= int(freeX) + int(freeY)) {
                mx = fx_;
                my = fy_;
                freeX = altX;
                freeY = altY;
            }
        }
    }

    // cada eje por separado, igual que el jugador: permite deslizarse por la
    // pared en vez de quedarse clavado contra una esquina
    if (freeX) e.x += mx;
    if (freeY) e.y += my;

    return 0;
}
