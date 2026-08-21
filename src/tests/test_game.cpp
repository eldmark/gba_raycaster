#include "Game.h"
#include "Raycaster.h"
#include "Textures.h"  // SPR_KIND_ITEM

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

// Distancia a la que el piloto deja de acercarse y empieza a retroceder
// disparando. El nucleo centinela pega a 1.8 celdas y va a un cuarto de la
// velocidad del jugador, asi que retroceder es la forma prevista de ganarle:
// un piloto que se planta delante y aguanta el intercambio no prueba que el
// jefe sea justo, solo que se puede morir de pie.
constexpr double KITE_DIST = 3.5;

// Da un frame de entrada apuntando a la celda destino. Con advance en false
// solo gira y dispara, sin acercarse: es lo que hace un jugador que tirotea a
// distancia en vez de meterse en el cuerpo a cuerpo. Con retreat se aleja
// mientras sigue apuntando.
bool steerToward(Game& game, int cx, int cy, bool advance = true,
                 bool shoot = true, bool retreat = false) {
    const Player& p = game.player();
    if (cellDist(p, cx, cy) < 0.25) {
        // ya encima del destino: se consume un frame igual disparando, o el
        // llamador podria girar en vacio para siempre
        Input in;
        in.fire = shoot;
        game.update(in, DT);
        return false;
    }

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
    if (advance && diff > -4000 && diff < 4000) in.fwd = true;
    if (retreat) in.back = true;
    // el gatillo va siempre apretado: el arma tiene su propia cadencia, y sin
    // disparar el piloto muere antes de llegar a la salida
    in.fire = shoot;

    game.update(in, DT);
    return true;
}

// Hay pared entre el jugador y el punto? Un enemigo tapado no se puede matar,
// asi que el piloto debe ignorarlo en vez de dispararle a la pared para siempre.
bool hasLineOfSight(const Game& game, fx tx, fx ty) {
    fx dx = tx - game.player().x;
    fx dy = ty - game.player().y;
    double len = std::hypot(double(dx) / FX_ONE, double(dy) / FX_ONE);
    if (len < 0.01) return true;
    Hit h = castRay(game.maze(), game.player().x, game.player().y, dx, dy);
    // castRay devuelve la distancia en unidades del vector que se le pasa, o
    // sea que 1.0 es justo el enemigo
    return h.perpDist > FX_ONE;
}

// Enemigo vivo mas cercano, o -1. El piloto lo usa para apuntar: sin esto no
// dispara a nada y el test solo probaria que se puede huir.
int nearestEnemy(const Game& game, double maxCells) {
    int best = -1;
    double bestD = maxCells;
    for (int i = 0; i < game.enemyCount(); ++i) {
        const Enemy& e = game.enemy(i);
        if (!e.alive()) continue;
        double dx = double(e.x - game.player().x) / FX_ONE;
        double dy = double(e.y - game.player().y) / FX_ONE;
        double d = std::hypot(dx, dy);
        if (d >= bestD) continue;
        if (!hasLineOfSight(game, e.x, e.y)) continue;
        bestD = d;
        best = i;
    }
    return best;
}

// Conduce al jugador hasta una celda concreta, disparando por el camino.
// Devuelve false si no llega, que es lo que delataria una ruta cortada.
bool driveTo(Game& game, int tx, int ty) {
    auto path = bfsPath(game.maze(), fxFloorInt(game.player().x),
                        fxFloorInt(game.player().y), tx, ty);
    if (path.empty()) return false;

    size_t waypoint = 1;
    for (int frames = 0; frames < 20000; ++frames) {
        if (game.state() != Game::State::Playing) return false;
        if (waypoint >= path.size()) return true;
        if (!steerToward(game, path[waypoint].first, path[waypoint].second)) {
            ++waypoint;
        }
    }
    return false;
}

// Lleva al jugador hasta la salida del piso actual. Devuelve false si se
// atasca, que significa que el piso no se puede terminar.
bool walkToExit(Game& game) {
    // La ruta entre archivos se muestra durante un instante; el piloto espera
    // la pantalla igual que lo haria un jugador antes de trazar la nueva ruta.
    int transitionFrames = 0;
    while (game.state() == Game::State::Transition && transitionFrames++ < 200) {
        Input idle;
        game.update(idle, DT);
    }
    if (game.state() != Game::State::Playing) return false;

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

        // Si hay un guardian cerca se le apunta y se le dispara; si no, se
        // sigue la ruta. Es lo minimo que hace un jugador real, y sin ello la
        // run no se puede terminar.
        int target = nearestEnemy(game, 8.0);
        if (target >= 0) {
            const Enemy& e = game.enemy(target);
            double dx = double(e.x - game.player().x) / FX_ONE;
            double dy = double(e.y - game.player().y) / FX_ONE;
            // acercarse solo si esta lejos; dentro de 3 celdas se dispara
            // quieto, que es como se juega de verdad
            const double d = std::hypot(dx, dy);
            steerToward(game, fxFloorInt(e.x), fxFloorInt(e.y), d > KITE_DIST,
                        true, d < KITE_DIST - 1.0);
            continue;
        }

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

    // Todo piso trae guardianes, y ninguno nace encima del jugador.
    {
        Game g;
        g.newRun(31337u);
        assert(g.aliveEnemies() > 0);
        for (int i = 0; i < g.enemyCount(); ++i) {
            const Enemy& e = g.enemy(i);
            assert(!g.maze().isWall(fxFloorInt(e.x), fxFloorInt(e.y)));
            double dx = double(e.x - g.player().x) / FX_ONE;
            double dy = double(e.y - g.player().y) / FX_ONE;
            assert(std::hypot(dx, dy) >= 4.0);
        }
    }

    // La seed 0 no es valida para el xorshift: debe sustituirse, no propagarse.
    {
        Game g;
        g.newRun(0);
        assert(g.seed() != 0);
        assert(g.maze().width() > 0);
    }

    // Dos runs con la misma seed son identicas, enemigos incluidos.
    {
        Game a, b, c;
        a.newRun(42u);
        b.newRun(42u);
        c.newRun(43u);
        assert(a.player().x == b.player().x && a.player().y == b.player().y);
        assert(a.enemyCount() == b.enemyCount());
        for (int i = 0; i < a.enemyCount(); ++i) {
            assert(a.enemy(i).x == b.enemy(i).x);
            assert(a.enemy(i).y == b.enemy(i).y);
        }
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

    // Quieto y lejos no pasa nada: los guardianes nacen fuera de su alcance de
    // vista y solo despiertan cuando el jugador se acerca. Un enemigo que
    // patrullara desde el principio echaria el nivel entero encima al entrar.
    {
        Game g;
        g.newRun(99u);
        Input idle;
        for (int i = 0; i < 600; ++i) g.update(idle, DT);
        assert(g.floor() == 1);
        assert(g.state() == Game::State::Playing);
        assert(g.hp() == Game::START_HP);
        for (int i = 0; i < g.enemyCount(); ++i) {
            assert(g.enemy(i).state == Enemy::State::Idle);
        }
    }

    // Acercarse SIN disparar cuesta vida: es la prueba de que el dano del
    // guardian llega al jugador y de que despierta al verlo.
    {
        Game g;
        g.newRun(99u);
        int target = nearestEnemy(g, 1e9);
        assert(target >= 0);
        int ex = fxFloorInt(g.enemy(target).x);
        int ey = fxFloorInt(g.enemy(target).y);

        auto path = bfsPath(g.maze(), fxFloorInt(g.player().x),
                            fxFloorInt(g.player().y), ex, ey);
        assert(!path.empty());

        size_t wp = 1;
        int guard = 0;
        while (g.hp() == Game::START_HP && guard++ < 20000 &&
               g.state() == Game::State::Playing) {
            if (wp >= path.size()) {
                // ya encima: quedarse ahi sin disparar
                Input idle;
                g.update(idle, DT);
                continue;
            }
            if (!steerToward(g, path[wp].first, path[wp].second, true, false)) ++wp;
        }
        assert(g.hp() < Game::START_HP);
    }

    // Los sprites que se mandan a dibujar son exactamente los enemigos vivos.
    {
        Game g;
        g.newRun(5150u);
        SpriteInstance sprites[Game::MAX_ENEMIES];
        int n = g.buildSprites(sprites, Game::MAX_ENEMIES);
        assert(n == g.aliveEnemies());
        for (int i = 0; i < n; ++i) {
            assert(sprites[i].frame >= 0 && sprites[i].frame < 2);
        }
    }

    // La sala final siempre contiene el protocolo visible que cambia de piso;
    // no es solo una celda X invisible. Los pickups normales lo acompañan.
    {
        Game g;
        g.newRun(5150u);
        SpriteInstance sprites[Game::MAX_ITEMS + 1];
        int n = g.buildItemSprites(sprites, Game::MAX_ITEMS + 1);
        bool protocol = false;
        for (int i = 0; i < n; ++i)
            protocol |= sprites[i].kind == SPR_KIND_ITEM + 4;
        assert(protocol);
    }

    // --- camara sellada -------------------------------------------------------
    // El recorrido completo del secreto: la camara nace cerrada, la llave esta
    // en otra sala, y solo tras recogerla y volver a la puerta se abre. Sin
    // este test la llave podria no hacer nada y el juego seguiria pasando el
    // resto de la suite.
    {
        // Se busca una seed cuyo piso 1 tenga camara: no todos los mapas dejan
        // sitio para una.
        Game g;
        uint32_t seed = 0;
        for (uint32_t s = 1; s <= 60 && seed == 0; ++s) {
            g.newRun(s * 7919u);
            if (!g.vaultOpen()) seed = s * 7919u;
        }
        assert(seed != 0);

        assert(!g.hasKey());
        assert(!g.vaultOpen());

        // Con la puerta cifrada el interior de la camara no se alcanza.
        int doorX = -1, doorY = -1;
        for (int y = 0; y < g.maze().height(); ++y) {
            for (int x = 0; x < g.maze().width(); ++x) {
                if (g.maze().at(x, y) == Maze::DOOR) { doorX = x; doorY = y; }
            }
        }
        assert(doorX >= 0);

        // La llave esta en el mundo como sprite recogible, no en un contador.
        SpriteInstance items[Game::MAX_ITEMS + 1];
        int n = g.buildItemSprites(items, Game::MAX_ITEMS + 1);
        int keyX = -1, keyY = -1;
        for (int i = 0; i < n; ++i) {
            if (items[i].kind != SPR_KIND_ITEM + 3) continue;
            keyX = fxFloorInt(items[i].x);
            keyY = fxFloorInt(items[i].y);
        }
        assert(keyX >= 0);

        // Se conduce hasta la llave con la misma Input de la plataforma.
        assert(driveTo(g, keyX, keyY));
        assert(g.hasKey());
        assert(!g.vaultOpen());  // tener la llave no abre nada a distancia

        // Y desde la llave hasta la puerta. Su celda es pared, asi que se
        // apunta a la casilla de suelo contigua desde la que se descifra.
        const int nx[4] = {1, -1, 0, 0}, ny[4] = {0, 0, 1, -1};
        bool arrived = false;
        for (int d = 0; d < 4 && !arrived; ++d) {
            const int ax = doorX + nx[d], ay = doorY + ny[d];
            if (g.maze().isWall(ax, ay)) continue;
            if (bfsPath(g.maze(), fxFloorInt(g.player().x),
                        fxFloorInt(g.player().y), ax, ay).empty()) {
                continue;  // ese lado es el interior sellado de la camara
            }
            arrived = driveTo(g, ax, ay);
        }
        assert(arrived);
        assert(g.vaultOpen());
        assert(!g.maze().isWall(doorX, doorY));  // la puerta es suelo de verdad
    }

    // La run se puede TERMINAR de verdad. El piloto recorre los cinco pisos
    // disparando a lo que se le cruza, y la run acaba en Cleared. Sin esto no
    // habria forma de saber si el juego es ganable o solo perdible despacio.
    {
        Game g;
        g.newRun(42u);
        for (int floor = 1; floor <= Game::FINAL_FLOOR; ++floor) {
            assert(g.floor() == floor);
            assert(!g.maze().isWall(fxFloorInt(g.player().x),
                                    fxFloorInt(g.player().y)));
            assert(walkToExit(g));
        }
        assert(g.state() == Game::State::Cleared);
        assert(g.hp() > 0);
        assert(g.kills() > 0);  // llego disparando, no escondiendose
        // El ultimo archivo no se gana esquivando: el protocolo solo responde
        // con el nucleo centinela abatido, asi que llegar a Cleared prueba que
        // el jefe cayo.
        assert(g.bossPresent());
        assert(!g.bossAlive());
    }

    std::printf("all tests passed\n");
    return 0;
}
