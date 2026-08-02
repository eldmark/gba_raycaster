#include "Player.h"

#include <cmath>

#include "Maze.h"

// Constantes portadas del original en pixeles (BLOCK_SIZE = 40) a celdas.
// El bucle de raylib corria fijo a 60 FPS, asi que lo por-frame pasa a por-segundo.
constexpr float MOVE_SPEED = 4.5f;      // 3.0 px/frame * 60 / 40
constexpr float ROTATION_SPEED = 3.0f;  // 0.05 rad/frame * 60
constexpr float RADIUS = 0.125f;        // 5.0 px / 40

bool collides(const Maze& maze, float x, float y) {
    const float off[4][2] = {{-RADIUS, 0.0f}, {RADIUS, 0.0f}, {0.0f, -RADIUS}, {0.0f, RADIUS}};
    for (const auto& o : off) {
        // floor y no truncado: -0.5 cae en la celda -1, no en la 0
        int i = static_cast<int>(std::floor(x + o[0]));
        int j = static_cast<int>(std::floor(y + o[1]));
        if (maze.at(i, j) != ' ') return true;
    }
    return false;
}

void updatePlayer(Player& player, const Input& input, const Maze& maze, float dt) {
    if (input.left) player.a -= ROTATION_SPEED * dt;
    if (input.right) player.a += ROTATION_SPEED * dt;

    float step = 0.0f;
    if (input.fwd) step = MOVE_SPEED * dt;
    if (input.back) step = -MOVE_SPEED * dt;
    if (step == 0.0f) return;

    float dx = step * std::cos(player.a);
    float dy = step * std::sin(player.a);

    // ponytail: se prueba cada eje por separado para poder deslizarse contra la pared
    if (!collides(maze, player.x + dx, player.y)) player.x += dx;
    if (!collides(maze, player.x, player.y + dy)) player.y += dy;
}
