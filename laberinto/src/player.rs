use raylib::prelude::*;

use crate::maze::Maze;

pub struct Player {
    pub pos: Vector2,
    pub a: f32,   // angle of view: a donde se va a mover en el siguiente movimiento
    pub fov: f32, // campo de vision en radianes
}

const MOVE_SPEED: f32 = 3.0;
const ROTATION_SPEED: f32 = 0.05;
const RADIUS: f32 = 5.0; // radio de colision del jugador en pixeles

pub fn process_events(player: &mut Player, rl: &RaylibHandle, maze: &Maze, block_size: usize) {
    if rl.is_key_down(KeyboardKey::KEY_LEFT) {
        player.a -= ROTATION_SPEED;
    }
    if rl.is_key_down(KeyboardKey::KEY_RIGHT) {
        player.a += ROTATION_SPEED;
    }

    let mut step = 0.0;
    if rl.is_key_down(KeyboardKey::KEY_UP) {
        step = MOVE_SPEED;
    }
    if rl.is_key_down(KeyboardKey::KEY_DOWN) {
        step = -MOVE_SPEED;
    }
    if step == 0.0 {
        return;
    }

    let dx = step * player.a.cos();
    let dy = step * player.a.sin();

    // ponytail: se prueba cada eje por separado para poder deslizarse contra la pared
    if !collides(maze, block_size, player.pos.x + dx, player.pos.y) {
        player.pos.x += dx;
    }
    if !collides(maze, block_size, player.pos.x, player.pos.y + dy) {
        player.pos.y += dy;
    }
}

pub(crate) fn collides(maze: &Maze, block_size: usize, x: f32, y: f32) -> bool {
    for (ox, oy) in [(-RADIUS, 0.0), (RADIUS, 0.0), (0.0, -RADIUS), (0.0, RADIUS)] {
        let i = ((x + ox) as i32) / block_size as i32;
        let j = ((y + oy) as i32) / block_size as i32;
        if i < 0 || j < 0 {
            return true;
        }
        match maze.get(j as usize).and_then(|row| row.get(i as usize)) {
            Some(' ') => {}
            Some(_) => return true,
            None => return true,
        }
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    fn maze() -> Maze {
        vec![
            "+++".chars().collect(),
            "+ +".chars().collect(),
            "+++".chars().collect(),
        ]
    }

    #[test]
    fn collision_rules() {
        let m = maze();
        // centro de la celda libre (1,1) con block_size 40
        assert!(!collides(&m, 40, 60.0, 60.0));
        // pegado a la pared de arriba
        assert!(collides(&m, 40, 60.0, 42.0));
        // fuera del laberinto
        assert!(collides(&m, 40, -10.0, 60.0));
        assert!(collides(&m, 40, 500.0, 60.0));
    }
}
