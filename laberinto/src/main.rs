mod caster;
mod framebuffer;
mod maze;
mod player;

use raylib::prelude::*;

use caster::cast_ray;
use framebuffer::Framebuffer;
use maze::{Maze, load_maze};
use player::{Player, process_events};

const BLOCK_SIZE: usize = 40;
const WIDTH: i32 = 900;
const HEIGHT: i32 = 600;
const MINIMAP_SCALE: f32 = 0.18;
const MINIMAP_MARGIN: i32 = 10;
const NUM_RAYS_MINIMAP: usize = 50;

fn wall_color(impact: char) -> Color {
    match impact {
        '+' => Color::SKYBLUE,
        '-' => Color::BLUE,
        '|' => Color::DARKBLUE,
        _ => Color::GRAY,
    }
}

/// Vista en primera persona: un rayo por columna de pantalla.
fn render_world(fb: &mut Framebuffer, maze: &Maze, player: &Player) {
    let half = HEIGHT / 2;

    // piso y cielo
    fb.set_current_color(Color::new(40, 40, 60, 255));
    fb.draw_rect(0, 0, WIDTH, half);
    fb.set_current_color(Color::new(70, 60, 50, 255));
    fb.draw_rect(0, half, WIDTH, HEIGHT - half);

    for x in 0..WIDTH {
        let ratio = x as f32 / WIDTH as f32;
        let a = player.a - player.fov / 2.0 + player.fov * ratio;
        let hit = cast_ray(maze, player, a, BLOCK_SIZE);

        // correccion de ojo de pez: proyectar sobre la direccion de la camara
        let d = (hit.distance * (a - player.a).cos()).max(1.0);
        let stake_height = (BLOCK_SIZE as f32 * HEIGHT as f32) / d;

        let top = (half as f32 - stake_height / 2.0).max(0.0) as i32;
        let bottom = (half as f32 + stake_height / 2.0).min(HEIGHT as f32) as i32;

        // oscurecer con la distancia
        let base = wall_color(hit.impact);
        let shade = (1.0 - (d / 500.0)).clamp(0.25, 1.0);
        fb.set_current_color(Color::new(
            (base.r as f32 * shade) as u8,
            (base.g as f32 * shade) as u8,
            (base.b as f32 * shade) as u8,
            255,
        ));
        fb.draw_rect(x, top, 1, bottom - top);
    }
}

/// Minimapa 2D en la esquina: laberinto, abanico de FOV y jugador.
#[allow(dead_code)]
fn render_minimap(fb: &mut Framebuffer, maze: &Maze, player: &Player) {
    let cell = (BLOCK_SIZE as f32 * MINIMAP_SCALE).max(1.0);
    let ox = MINIMAP_MARGIN;
    let oy = MINIMAP_MARGIN;
    // world -> minimap
    let to_map = |x: f32, y: f32| {
        (
            ox + (x * MINIMAP_SCALE) as i32,
            oy + (y * MINIMAP_SCALE) as i32,
        )
    };

    let cols = maze.iter().map(|r| r.len()).max().unwrap_or(0);
    fb.set_current_color(Color::new(0, 0, 0, 255));
    fb.draw_rect(
        ox - 2,
        oy - 2,
        (cols as f32 * cell) as i32 + 4,
        (maze.len() as f32 * cell) as i32 + 4,
    );

    fb.set_current_color(Color::SKYBLUE);
    for (j, row) in maze.iter().enumerate() {
        for (i, &c) in row.iter().enumerate() {
            if c == ' ' {
                continue;
            }
            fb.draw_rect(
                ox + (i as f32 * cell) as i32,
                oy + (j as f32 * cell) as i32,
                cell as i32,
                cell as i32,
            );
        }
    }

    fb.set_current_color(Color::RED);
    let (px, py) = to_map(player.pos.x, player.pos.y);
    for i in 0..NUM_RAYS_MINIMAP {
        let ratio = i as f32 / NUM_RAYS_MINIMAP as f32;
        let a = player.a - player.fov / 2.0 + player.fov * ratio;
        let hit = cast_ray(maze, player, a, BLOCK_SIZE);
        let (hx, hy) = to_map(
            player.pos.x + hit.distance * a.cos(),
            player.pos.y + hit.distance * a.sin(),
        );
        fb.draw_line(px, py, hx, hy);
    }

    fb.set_current_color(Color::YELLOW);
    fb.draw_rect(px - 2, py - 2, 4, 4);
}

fn main() {
    let maze = load_maze("maze.txt");

    let (mut window, thread) = raylib::init()
        .size(WIDTH, HEIGHT)
        .title("Laberinto")
        .build();
    window.set_target_fps(60);

    let mut fb = Framebuffer::new(WIDTH, HEIGHT, Color::BLACK);

    let mut player = Player {
        pos: Vector2::new(BLOCK_SIZE as f32 * 1.5, BLOCK_SIZE as f32 * 1.5),
        a: std::f32::consts::PI / 3.0,
        fov: std::f32::consts::PI / 3.0,
    };

    while !window.window_should_close() {
        process_events(&mut player, &window, &maze, BLOCK_SIZE);

        fb.clear();
        render_world(&mut fb, &maze, &player);
        // render_minimap(&mut fb, &maze, &player);
        fb.swap_buffers(&mut window, &thread);
    }
}
