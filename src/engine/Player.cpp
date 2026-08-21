#include "Player.h"

#include <algorithm>

#include "Maze.h"

// Constantes portadas del original en pixeles (BLOCK_SIZE = 40) a celdas.
// El bucle de raylib corria fijo a 60 FPS, asi que lo por-frame pasa a por-segundo.
namespace {
constexpr fx MOVE_SPEED = fxFloat(4.5f);   // 3.0 px/frame * 60 / 40
constexpr fx RADIUS = fxFloat(0.125f);     // 5.0 px / 40

// 3.0 rad/s pasados a la unidad de angle: 3.0 / (2*PI) * 65536.
constexpr int32_t ROTATION_SPEED = 31294;  // unidades de angulo por segundo

// Tope de la inclinacion, en fracciones de media pantalla. Mas alla de esto el
// desplazamiento del horizonte deja de leerse como mirar arriba y empieza a
// leerse como que la imagen se desliza: las paredes son verticales de verdad y
// no se inclinan con la vista.
constexpr fx MAX_PITCH = fxFloat(0.6f);
}  // namespace

bool collides(const Maze& maze, fx x, fx y) {
    const fx off[4][2] = {{-RADIUS, 0}, {RADIUS, 0}, {0, -RADIUS}, {0, RADIUS}};
    for (const auto& o : off) {
        // el shift aritmetico de fxFloorInt redondea hacia abajo tambien con
        // negativos: -0.5 cae en la celda -1, no en la 0
        // isWall y no una comparacion propia: que cuenta como suelo lo decide
        // Maze en un solo sitio, si no una textura nueva vuelve solida media
        // pared sin que nadie lo note
        if (maze.isWall(fxFloorInt(x + o[0]), fxFloorInt(y + o[1]))) return true;
    }
    return false;
}

void updatePlayer(Player& player, const Input& input, const Maze& maze, fx dt) {
    // dt ya es 16.16, asi que el >> FX_BITS deja el giro en unidades de angulo
    angle turn = angle(fxMul(ROTATION_SPEED, dt));
    if (input.left) player.a -= turn;
    if (input.right) player.a += turn;
    // El giro analogico se suma encima y NO se escala por dt: el raton ya da
    // un desplazamiento por frame, no una velocidad, y multiplicarlo por dt
    // haria que la mira dependiera de los FPS.
    player.a += angle(input.turn);

    // Igual que el giro, la inclinacion llega ya como desplazamiento por frame
    // y no se escala por dt aqui.
    player.pitch = std::clamp(player.pitch + input.look, -MAX_PITCH, MAX_PITCH);

    fx step = 0;
    if (input.fwd) step = fxMul(MOVE_SPEED, dt);
    if (input.back) step = -fxMul(MOVE_SPEED, dt);
    // El stick manda sobre las teclas cuando esta fuera de la zona muerta: si
    // no, empujarlo a medias daria la misma velocidad que empujarlo del todo.
    if (input.thrust != 0) step = fxMul(fxMul(MOVE_SPEED, dt), input.thrust);
    if (step == 0) return;

    fx dx = fxMul(step, fxCos(player.a));
    fx dy = fxMul(step, fxSin(player.a));

    // ponytail: se prueba cada eje por separado para poder deslizarse contra la pared
    if (!collides(maze, player.x + dx, player.y)) player.x += dx;
    if (!collides(maze, player.x, player.y + dy)) player.y += dy;
}
