#include "Game.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>
#include <vector>

namespace {

constexpr fx DT = FX_ONE / 60;

// --- piloto automatico --------------------------------------------------------
// Conduce al jugador hasta la salida usando la MISMA Input que la plataforma.
// No mueve al jugador a mano: si las colisiones o el giro estuvieran rotos, el
// piloto se quedaria atascado y el test fallaria, que es justo lo que se busca.

// Ruta mas corta en celdas desde (sx,sy) hasta (tx,ty). Vacia si no hay.
std::vector<std::pair<int, int>> bfsPath(const Maze& m, int sx, int sy, int tx, int ty) {
    const int w = m.width(), h = m.height();
    std::vector<int> prev(size_t(w) * size_t(h), -2);
    std::queue<int> q;
    int start = sy * w + sx;
    prev[size_t(start)] = -1;
    q.push(start);

    while (!q.empty()) {
        int cur = q.front();
        q.pop();
        if (cur == ty * w + tx) break;
        int cx = cur % w, cy = cur / w;
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (m.isWall(nx, ny)) continue;
            if (prev[size_t(ny * w + nx)] != -2) continue;
            prev[size_t(ny * w + nx)] = cur;
            q.push(ny * w + nx);
        }
    }

    std::vector<std::pair<int, int>> path;
    if (prev[size_t(ty * w + tx)] == -2) return path;
    for (int cur = ty * w + tx; cur != -1; cur = prev[size_t(cur)]) {
        path.push_back({cur % w, cur / w});
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// Busca la salida del piso actual recorriendo el mapa.
bool findExit(const Maze& m, int& ex, int& ey) {
    for (int y = 0; y < m.height(); ++y) {
        for (int x = 0; x < m.width(); ++x) {
            if (m.at(x, y) == Maze::EXIT) { ex = x; ey = y; return true; }
        }
    }
    return false;
}

double cellDist(const Player& p, int cx, int cy) {
    double px = double(p.x) / FX_ONE, py = double(p.y) / FX_ONE;
    return std::hypot(px - (cx + 0.5), py - (cy + 0.5));
}

// Da un frame de entrada apuntando a la celda destino. Devuelve false si el
// jugador ya esta encima.
bool steerToward(Game& game, int cx, int cy) {
    const Player& p = game.player();
    if (cellDist(p, cx, cy) < 0.25) return false;

    double px = double(p.x) / FX_ONE, py = double(p.y) / FX_ONE;
    double want = std::atan2((cy + 0.5) - py, (cx + 0.5) - px);
    angle target = angle(int32_t(want * (65536.0 / (2.0 * M_PI))) & 0xFFFF);
    // resta en int16: da el giro mas corto, con signo, sin normalizar nada
    int16_t diff = int16_t(target - p.a);

    Input in;
    if (diff > 900) in.right = true;
    else if (diff < -900) in.left = true;
    // se avanza solo cuando ya se mira casi hacia el destino, o el jugador
    // describe arcos y se engancha en las esquinas
    if (diff > -4000 && diff < 4000) in.fwd = true;

    game.update(in, DT);
    return true;
}

// Lleva al jugador hasta la salida del piso actual. Devuelve false si se
// atasca, que significa que el piso no se puede terminar.
bool walkToExit(Game& game) {
    int ex, ey;
    if (!findExit(game.maze(), ex, ey)) return false;

    auto path = bfsPath(game.maze(), fxFloorInt(game.player().x),
                        fxFloorInt(game.player().y), ex, ey);
    if (path.empty()) return false;

    const int startFloor = game.floor();
    size_t waypoint = 1;  // [0] es la celda donde ya esta
    int frames = 0;

    while (frames++ < 20000) {
        // cambiar de piso o terminar la run es el exito que se buscaba
        if (game.floor() != startFloor || game.state() != Game::State::Playing) {
            return true;
        }
        if (waypoint >= path.size()) return false;
        if (!steerToward(game, path[waypoint].first, path[waypoint].second)) {
            ++waypoint;
        }
    }
    return false;
}

}  // namespace

int main() {
    // Una run arranca en el piso 1, con la vida llena y viva.
    Game game;
    game.newRun(583291u);
    assert(game.state() == Game::State::Playing);
    assert(game.floor() == 1);
    assert(game.hp() == Game::START_HP);
    assert(game.kills() == 0);
    assert(game.seed() == 583291u);

    // La seed 0 no es valida para el xorshift: debe sustituirse, no propagarse.
    {
        Game g;
        g.newRun(0);
        assert(g.seed() != 0);
        assert(g.maze().width() > 0);
    }

    // Dos runs con la misma seed son identicas; con otra seed, no.
    {
        Game a, b, c;
        a.newRun(42u);
        b.newRun(42u);
        c.newRun(43u);
        assert(a.player().x == b.player().x && a.player().y == b.player().y);
        bool same = a.maze().width() == b.maze().width();
        for (int y = 0; y < a.maze().height() && same; ++y) {
            for (int x = 0; x < a.maze().width() && same; ++x) {
                same = a.maze().at(x, y) == b.maze().at(x, y);
            }
        }
        assert(same);
        assert(c.player().x != a.player().x || c.player().y != a.player().y ||
               c.maze().width() != a.maze().width());
    }

    // Un dt gigante no puede atravesar paredes: es el caso del arrastre de
    // ventana o de un breakpoint.
    {
        Game g;
        g.newRun(7u);
        Input fwd;
        fwd.fwd = true;
        for (int i = 0; i < 400; ++i) {
            g.update(fwd, FX_ONE / 4);
            assert(!g.maze().isWall(fxFloorInt(g.player().x),
                                    fxFloorInt(g.player().y)));
        }
    }

    // Quedarse quieto no cambia de piso ni mata a nadie.
    {
        Game g;
        g.newRun(99u);
        Input idle;
        for (int i = 0; i < 600; ++i) g.update(idle, DT);
        assert(g.floor() == 1);
        assert(g.state() == Game::State::Playing);
        assert(g.hp() == Game::START_HP);
    }

    // La run se puede terminar de verdad: se recorren los 5 pisos caminando,
    // no teletransportando, y la run acaba en Cleared. Es la prueba de que el
    // MVP es jugable de principio a fin.
    {
        Game g;
        g.newRun(583291u);
        for (int floor = 1; floor <= Game::FINAL_FLOOR; ++floor) {
            assert(g.floor() == floor);
            assert(!g.maze().isWall(fxFloorInt(g.player().x),
                                    fxFloorInt(g.player().y)));
            assert(walkToExit(g));
        }
        assert(g.state() == Game::State::Cleared);
        assert(g.hp() > 0);
    }

    std::printf("all tests passed\n");
    return 0;
}
