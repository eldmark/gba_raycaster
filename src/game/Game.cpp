#include "Game.h"

namespace {
// fov de 60 grados guardado ya como tan(fov/2), que es lo que consume la camara
constexpr fx FOV_TAN_HALF = fxFloat(0.57735027f);
}  // namespace

void Game::newRun(uint32_t seed) {
    seed_ = seed ? seed : 1u;
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
}

bool Game::onExit() const {
    return maze_.at(fxFloorInt(player_.x), fxFloorInt(player_.y)) == Maze::EXIT;
}

void Game::update(const Input& input, fx dt) {
    // muerto o terminado, el mundo se congela: quien llama decide si reinicia
    if (state_ != State::Playing) return;

    updatePlayer(player_, input, maze_, dt);

    if (hp_ <= 0) {
        hp_ = 0;
        state_ = State::Dead;
        return;
    }

    if (onExit()) {
        if (floor_ >= FINAL_FLOOR) {
            state_ = State::Cleared;
        } else {
            loadFloor(floor_ + 1);
        }
    }
}
