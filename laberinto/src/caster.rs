use crate::maze::Maze;
use crate::player::Player;

pub struct Intersect {
    pub distance: f32,
    pub impact: char,
}

/// Lanza un rayo desde el jugador con el angulo dado y devuelve donde choca.
pub fn cast_ray(maze: &Maze, player: &Player, a: f32, block_size: usize) -> Intersect {
    // ponytail: avance a pasos fijos, no DDA. Suficiente a esta resolucion;
    // cambiar a DDA si aparecen artefactos o baja el framerate.
    let mut d = 0.0f32;
    let (cos, sin) = (a.cos(), a.sin());

    loop {
        let x = player.pos.x + d * cos;
        let y = player.pos.y + d * sin;

        let i = x as i32 / block_size as i32;
        let j = y as i32 / block_size as i32;

        if x < 0.0 || y < 0.0 {
            return Intersect { distance: d, impact: '+' };
        }

        match maze.get(j as usize).and_then(|row| row.get(i as usize)) {
            Some(' ') => {}
            Some(&c) => return Intersect { distance: d, impact: c },
            None => return Intersect { distance: d, impact: '+' },
        }

        d += 1.0;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use raylib::prelude::Vector2;

    #[test]
    fn ray_hits_wall_at_expected_distance() {
        let maze: Maze = vec![
            "+++".chars().collect(),
            "+ +".chars().collect(),
            "+++".chars().collect(),
        ];
        let player = Player {
            pos: Vector2::new(60.0, 60.0), // centro de la celda libre
            a: 0.0,
            fov: 1.0,
        };

        // hacia la derecha: pared en x=80, jugador en x=60 -> ~20px
        let hit = cast_ray(&maze, &player, 0.0, 40);
        assert!((hit.distance - 20.0).abs() <= 1.0, "distance {}", hit.distance);
        assert_eq!(hit.impact, '+');
    }
}
