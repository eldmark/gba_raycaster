use raylib::prelude::*;

pub struct Framebuffer {
    pub width: i32,
    pub height: i32,
    color_buffer: Image,
    background_color: Color,
    current_color: Color,
}

impl Framebuffer {
    pub fn new(width: i32, height: i32, background_color: Color) -> Self {
        Framebuffer {
            width,
            height,
            color_buffer: Image::gen_image_color(width, height, background_color),
            background_color,
            current_color: Color::WHITE,
        }
    }

    pub fn clear(&mut self) {
        self.color_buffer =
            Image::gen_image_color(self.width, self.height, self.background_color);
    }

    pub fn set_current_color(&mut self, color: Color) {
        self.current_color = color;
    }

    pub fn set_pixel(&mut self, x: i32, y: i32) {
        if x >= 0 && x < self.width && y >= 0 && y < self.height {
            self.color_buffer.draw_pixel(x, y, self.current_color);
        }
    }

    pub fn draw_rect(&mut self, x: i32, y: i32, w: i32, h: i32) {
        for dy in 0..h {
            for dx in 0..w {
                self.set_pixel(x + dx, y + dy);
            }
        }
    }

    pub fn draw_line(&mut self, x0: i32, y0: i32, x1: i32, y1: i32) {
        let steps = (x1 - x0).abs().max((y1 - y0).abs()).max(1);
        for s in 0..=steps {
            let t = s as f32 / steps as f32;
            let x = x0 as f32 + (x1 - x0) as f32 * t;
            let y = y0 as f32 + (y1 - y0) as f32 * t;
            self.set_pixel(x as i32, y as i32);
        }
    }

    /// Dump the buffer to the window as a texture.
    pub fn swap_buffers(&self, window: &mut RaylibHandle, thread: &RaylibThread) {
        if let Ok(texture) = window.load_texture_from_image(thread, &self.color_buffer) {
            let mut d = window.begin_drawing(thread);
            d.clear_background(self.background_color);
            d.draw_texture(&texture, 0, 0, Color::WHITE);
        }
    }
}
