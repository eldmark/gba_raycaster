#include "Enemy.h"
#include "Maze.h"

#include <cassert>
#include <cstdio>

namespace {

constexpr fx DT = FX_ONE / 60;

// Sala de 9x9 vacia con muros alrededor, para probar la IA sin generador.
Maze openRoom() {
    Maze m;
    m.reset(9, 9, Maze::FLOOR);
    for (int i = 0; i < 9; ++i) {
        m.set(i, 0, Maze::WALL);
        m.set(i, 8, Maze::WALL);
        m.set(0, i, Maze::WALL);
        m.set(8, i, Maze::WALL);
    }
    return m;
}

Enemy at(fx x, fx y, const EnemyTuning& t) {
    Enemy e{};
    e.x = x;
    e.y = y;
    e.hp = t.hp;
    e.state = Enemy::State::Idle;
    return e;
}

double cells(fx v) { return double(v) / FX_ONE; }

}  // namespace

int main() {
    const EnemyTuning t = tuningForFloor(1);
    const Maze room = openRoom();

    // La dificultad sube con el piso, pero la vista no: un guardian del piso 5
    // pega mas fuerte, no ve mas lejos.
    {
        EnemyTuning t5 = tuningForFloor(5);
        assert(t5.damage > t.damage);
        assert(t5.hp > t.hp);
        assert(t5.speed > t.speed);
        assert(t5.sightRange == t.sightRange);
    }

    // Fuera del alcance de la vista se queda quieto: si patrullara sin ver al
    // jugador, el nivel entero se le echaria encima al entrar.
    {
        Enemy e = at(fxFloat(1.5f), fxFloat(1.5f), t);
        fx px = fxFloat(1.5f) + t.sightRange + fxInt(2);
        for (int i = 0; i < 120; ++i) {
            assert(updateEnemy(e, room, px, fxFloat(1.5f), t, DT) == 0);
        }
        assert(e.state == Enemy::State::Idle);
        assert(e.x == fxFloat(1.5f) && e.y == fxFloat(1.5f));
    }

    // Dentro de la vista persigue y se acerca.
    {
        Enemy e = at(fxFloat(1.5f), fxFloat(4.5f), t);
        const fx px = fxFloat(6.5f), py = fxFloat(4.5f);
        double before = cells(px - e.x);
        for (int i = 0; i < 60; ++i) updateEnemy(e, room, px, py, t, DT);
        assert(e.state != Enemy::State::Idle);
        assert(cells(px - e.x) < before);
    }

    // Pegado al jugador pega, y respeta su cadencia: no puede vaciar la vida
    // en un solo frame.
    {
        Enemy e = at(fxFloat(4.5f), fxFloat(4.5f), t);
        const fx px = fxFloat(5.0f), py = fxFloat(4.5f);
        int hits = 0, total = 0;
        // dos segundos exactos: con periodo 1.2 s caben dos golpes
        for (int i = 0; i < 120; ++i) {
            int dmg = updateEnemy(e, room, px, py, t, DT);
            if (dmg > 0) { ++hits; total += dmg; }
        }
        assert(e.state == Enemy::State::Attack);
        assert(hits == 2);
        assert(total == 2 * t.damage);
    }

    // Nunca atraviesa una pared, por mucho que el jugador este al otro lado.
    {
        Maze split = openRoom();
        for (int y = 1; y < 8; ++y) split.set(4, y, Maze::WALL);

        Enemy e = at(fxFloat(2.5f), fxFloat(4.5f), t);
        const fx px = fxFloat(6.5f), py = fxFloat(4.5f);
        for (int i = 0; i < 600; ++i) {
            updateEnemy(e, split, px, py, t, DT);
            assert(!split.isWall(fxFloorInt(e.x), fxFloorInt(e.y)));
        }
        assert(cells(e.x) < 4.0);  // sigue de su lado del muro
    }

    // Un guardian muerto no se mueve, no pega y no revive.
    {
        Enemy e = at(fxFloat(4.5f), fxFloat(4.5f), t);
        e.state = Enemy::State::Dead;
        fx x0 = e.x;
        for (int i = 0; i < 120; ++i) {
            assert(updateEnemy(e, room, fxFloat(5.0f), fxFloat(4.5f), t, DT) == 0);
        }
        assert(e.state == Enemy::State::Dead);
        assert(e.x == x0);
    }

    // El latido del sprite alterna entre los dos fotogramas.
    {
        Enemy e = at(fxFloat(1.5f), fxFloat(1.5f), t);
        const fx far = fxFloat(1.5f) + t.sightRange + fxInt(2);
        bool seen[2] = {false, false};
        for (int i = 0; i < 240; ++i) {
            updateEnemy(e, room, far, fxFloat(1.5f), t, DT);
            seen[e.frame & 1] = true;
        }
        assert(seen[0] && seen[1]);
    }

    std::printf("all tests passed\n");
    return 0;
}
