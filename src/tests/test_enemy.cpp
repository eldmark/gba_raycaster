#include "Enemy.h"
#include "Maze.h"
#include "Nav.h"

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

// Un paso de guardian con el campo de flujo al dia, como lo hace Game.
int step(Enemy& e, const Maze& m, Nav& nav, fx px, fx py, const EnemyTuning& t) {
    nav.rebuild(m, fxFloorInt(px), fxFloorInt(py));
    return updateEnemy(e, m, nav, px, py, t, DT);
}

}  // namespace

int main() {
    const EnemyTuning t = tuningForFloor(1);
    const Maze room = openRoom();
    Nav nav;

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
            assert(step(e, room, nav, px, fxFloat(1.5f), t) == 0);
        }
        assert(e.state == Enemy::State::Idle);
        assert(e.x == fxFloat(1.5f) && e.y == fxFloat(1.5f));
    }

    // Dentro de la vista persigue y se acerca.
    {
        Enemy e = at(fxFloat(1.5f), fxFloat(4.5f), t);
        const fx px = fxFloat(6.5f), py = fxFloat(4.5f);
        double before = cells(px - e.x);
        for (int i = 0; i < 60; ++i) step(e, room, nav, px, py, t);
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
            int dmg = step(e, room, nav, px, py, t);
            if (dmg > 0) { ++hits; total += dmg; }
        }
        assert(e.state == Enemy::State::Attack);
        assert(hits == 2);
        assert(total == 2 * t.damage);
    }

    // Nunca atraviesa una pared, pero SI la rodea. Antes del campo de flujo se
    // quedaba empujando el muro para siempre; en 60 semillas de prueba eso le
    // pasaba a 10 de los 25 guardianes que llegaban a despertar.
    {
        Maze split = openRoom();
        for (int y = 1; y < 7; ++y) split.set(4, y, Maze::WALL);  // hueco en y=7

        Enemy e = at(fxFloat(2.5f), fxFloat(2.5f), t);
        e.state = Enemy::State::Chase;  // ya despierto, al otro lado
        const fx px = fxFloat(6.5f), py = fxFloat(2.5f);

        bool reached = false;
        for (int i = 0; i < 1200 && !reached; ++i) {
            step(e, split, nav, px, py, t);
            assert(!split.isWall(fxFloorInt(e.x), fxFloorInt(e.y)));
            reached = cells(e.x) > 5.0;  // cruzo por el hueco de abajo
        }
        assert(reached);
    }

    // A traves de una pared no despierta: ver, no solo estar cerca.
    {
        Maze split = openRoom();
        for (int y = 1; y < 8; ++y) split.set(4, y, Maze::WALL);

        Enemy e = at(fxFloat(2.5f), fxFloat(4.5f), t);
        const fx px = fxFloat(6.5f), py = fxFloat(4.5f);  // a 4 celdas, sin verlo
        for (int i = 0; i < 300; ++i) step(e, split, nav, px, py, t);
        assert(e.state == Enemy::State::Idle);
    }

    // Si el jugador esta incomunicado, el guardian se rinde en vez de empujar
    // el muro eternamente.
    {
        Maze sealed = openRoom();
        for (int y = 1; y < 8; ++y) sealed.set(4, y, Maze::WALL);  // sin hueco

        Enemy e = at(fxFloat(2.5f), fxFloat(4.5f), t);
        e.state = Enemy::State::Chase;
        const fx px = fxFloat(6.5f), py = fxFloat(4.5f);
        for (int i = 0; i < 120; ++i) step(e, sealed, nav, px, py, t);
        assert(e.state == Enemy::State::Idle);
    }

    // Un guardian muerto no se mueve, no pega y no revive.
    {
        Enemy e = at(fxFloat(4.5f), fxFloat(4.5f), t);
        e.state = Enemy::State::Dead;
        fx x0 = e.x;
        for (int i = 0; i < 120; ++i) {
            assert(step(e, room, nav, fxFloat(5.0f), fxFloat(4.5f), t) == 0);
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
            step(e, room, nav, far, fxFloat(1.5f), t);
            seen[e.frame & 1] = true;
        }
        assert(seen[0] && seen[1]);
    }

    std::printf("all tests passed\n");
    return 0;
}
