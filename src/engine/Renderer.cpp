#include "Renderer.h"

#include <algorithm>

#include "Fixed.h"

#include "Framebuffer.h"
#include "Maze.h"
#include "Player.h"
#include "Raycaster.h"
#include "Textures.h"

namespace {

// El Rust oscurecia con (1 - d/500) sobre distancias en pixeles de mundo.
// En celdas, 500 px / 40 px por celda = 12.5 celdas. Se guarda el reciproco
// para que el sombreado sea una multiplicacion y no una division.
constexpr fx INV_SHADE_RANGE = fxFloat(1.0f / 12.5f);
constexpr fx MIN_LIGHT = fxFloat(0.25f);
constexpr fx SIDE_LIGHT = fxFloat(0.7f);

// Lineas de rejilla del suelo, contadas en medias celdas. Las enteras solas
// dejan vacio el campo cercano, porque la primera cae justo en el borde de
// abajo de la pantalla; mas alla de estas, todas caen sobre la misma fila.
constexpr int GRID_STEPS = 20;

// Ancho maximo de pantalla soportado. Solo dimensiona el z-buffer; en GBA
// bastan 240 y el array baja a 960 bytes.
// ponytail: array fijo en vez de reservar por frame, que en GBA no hay heap
// que valga la pena tocar en el bucle de render.
#ifndef MAX_SCREEN_W
#define MAX_SCREEN_W 960
#endif

// Distancia de la pared de cada columna, rellenada por renderWorld y consultada
// por renderSprites (seccion 22 del PROJECT.md).
fx g_wallDist[MAX_SCREEN_W];
int g_zbufWidth = 0;

// Devuelve el NIVEL de la rampa, no un factor: el sombreado ya esta horneado en
// la paleta, asi que el bucle interno solo suma este entero al indice.
int shadeLevel(fx perpDist, int side) {
    fx light = FX_ONE - fxMul(perpDist, INV_SHADE_RANGE);
    light = std::clamp(light, MIN_LIGHT, FX_ONE);
    // las caras horizontales van mas oscuras, como en Wolf3D: da volumen sin
    // necesidad de iluminacion real.
    if (side == 1) light = fxMul(light, SIDE_LIGHT);
    return std::clamp(int((light * SHADE_LEVELS) >> FX_BITS), 0, SHADE_LEVELS - 1);
}

// Base de camara de Wolf3D: dir mira al frente, plane es perpendicular y mide
// tan(fov/2). rayDir = dir + plane * cameraX barre el FOV exacto, y de yapa
// deja el vector sin normalizar para que el t del DDA salga perpendicular.
struct Camera {
    fx dirX, dirY;
    fx planeX, planeY;
};

Camera cameraOf(const Player& p) {
    // p.fov ya viene como tan(fov/2): la tangente se calcula al crear al
    // jugador, no una vez por frame.
    fx dirX = fxCos(p.a);
    fx dirY = fxSin(p.a);
    return {dirX, dirY, -fxMul(dirY, p.fov), fxMul(dirX, p.fov)};
}

}  // namespace

void renderWorld(Framebuffer& fb, const Maze& maze, const Player& player) {
    const int w = fb.width();
    const int h = fb.height();
    const int half = h / 2;
    const Camera cam = cameraOf(player);

    // cielo y piso en bandas de paleta: degradan sin costar mas que 2*BG_LEVELS
    // fillRect por frame, y en GBA el degradado se puede pasar a HBlank DMA.
    for (int b = 0; b < BG_LEVELS; ++b) {
        int y0 = half * b / BG_LEVELS;
        int y1 = half * (b + 1) / BG_LEVELS;
        fb.fillRect(0, y0, w, y1 - y0, uint8_t(PAL_SKY + b));

        int f0 = half + (h - half) * b / BG_LEVELS;
        int f1 = half + (h - half) * (b + 1) / BG_LEVELS;
        fb.fillRect(0, f0, w, f1 - f0, uint8_t(PAL_FLOOR + b));
    }

    // Rejilla del suelo. Las lineas van a distancias de mundo enteras, y una
    // pared a distancia d se proyecta a h/d, asi que la fila de la linea sale
    // de la misma division: quedan juntas cerca del horizonte y separadas a los
    // pies, que es lo que hace que se lea como perspectiva y no como rayas.
    for (int i = 3; i <= GRID_STEPS; ++i) {
        int y = half + 2 * (h - half) / i;  // i son medias celdas de distancia
        if (y >= h) continue;
        int level = std::clamp(BG_LEVELS - i / 2, 0, BG_LEVELS - 1);
        fb.fillRect(0, y, w, 1, uint8_t(PAL_FLOOR_LINE + level));
    }

    // cameraX barre [-1, 1) de a pasos iguales. Incremental para no pagar una
    // division por columna: la unica que queda es la de la altura.
    const fx cameraStep = fxDiv(2 * FX_ONE, fxInt(w));
    fx cameraX = -FX_ONE;

    // TEX_SIZE/h es constante, asi que el avance en textura sale de una
    // multiplicacion por perpDist en vez de dividir entre la altura proyectada.
    const fx texPerDist = fxDiv(fxInt(TEX_SIZE), fxInt(h));

    for (int x = 0; x < w; ++x, cameraX += cameraStep) {
        fx rayX = cam.dirX + fxMul(cam.planeX, cameraX);
        fx rayY = cam.dirY + fxMul(cam.planeY, cameraX);

        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);

        // altura proyectada; equivale al BLOCK_SIZE*HEIGHT/d del Rust porque
        // alla d estaba en pixeles de mundo y aca perpDist esta en celdas.
        fx stakeHeight = fxDiv(fxInt(h), hit.perpDist);
        fx exactTop = fxInt(half) - (stakeHeight >> 1);
        int top = std::max(0, fxFloorInt(exactTop));
        int bottom = std::min(h, fxFloorInt(fxInt(half) + (stakeHeight >> 1)));

        // caras que miran a -X / -Y se espejan, si no la textura sale invertida
        // al rodear una esquina y se nota la costura.
        fx u = hit.wallX;
        if ((hit.side == 0 && rayX < 0) || (hit.side == 1 && rayY > 0)) {
            u = FX_ONE - 1 - u;
        }
        int texX = (u * TEX_SIZE) >> FX_BITS;

        const int texId = texIndex(hit.impact);
        const Texture& tex = textures()[texId];
        // Tabla de 4 entradas, una por color del texel, calculada una vez por
        // columna: el bucle interno de abajo solo indexa, no multiplica ni
        // busca la rampa deduplicada por pixel (ver wallColumnBase en
        // Textures.h).
        uint8_t colBase[TEX_COLORS];
        wallColumnBase(texId, shadeLevel(hit.perpDist, hit.side), colBase);

        // avance en la textura por pixel de pantalla. texPos arranca desde
        // exactTop y no desde top, asi la textura no "resbala" cuando la pared
        // se sale por arriba de la pantalla.
        fx step = fxMul(hit.perpDist, texPerDist);
        fx texPos = fxMul(fxInt(top) - exactTop, step);

        const uint8_t* col = tex.px + texX;
        for (int y = top; y < bottom; ++y, texPos += step) {
            int texY = fxFloorInt(texPos) & (TEX_SIZE - 1);
            fb.setPixel(x, y, colBase[col[texY * TEX_SIZE]]);
        }

        if (x < MAX_SCREEN_W) g_wallDist[x] = hit.perpDist;
    }
    g_zbufWidth = std::min(w, MAX_SCREEN_W);
}

void renderSprites(Framebuffer& fb, const Player& player,
                   SpriteInstance* sprites, int count) {
    const int w = fb.width();
    const int h = fb.height();
    const int half = h / 2;
    const Camera cam = cameraOf(player);

    // De lejos a cerca: si no, un enemigo lejano se dibuja encima de uno
    // cercano. Insercion porque count es de un digito y ya suele venir casi
    // ordenado de un frame al siguiente.
    for (int i = 1; i < count; ++i) {
        SpriteInstance key = sprites[i];
        fx keyD = fxMul(key.x - player.x, key.x - player.x) +
                  fxMul(key.y - player.y, key.y - player.y);
        int j = i - 1;
        while (j >= 0) {
            fx d = fxMul(sprites[j].x - player.x, sprites[j].x - player.x) +
                   fxMul(sprites[j].y - player.y, sprites[j].y - player.y);
            if (d >= keyD) break;
            sprites[j + 1] = sprites[j];
            --j;
        }
        sprites[j + 1] = key;
    }

    // Cambio a coordenadas de camara. La matriz [plane | dir] lleva de camara a
    // mundo, asi que su inversa lleva de mundo a camara: transY sale siendo la
    // profundidad, comparable directamente con el z-buffer.
    const fx det = fxMul(cam.planeX, cam.dirY) - fxMul(cam.dirX, cam.planeY);
    if (det == 0) return;

    for (int i = 0; i < count; ++i) {
        const fx relX = sprites[i].x - player.x;
        const fx relY = sprites[i].y - player.y;

        const fx transX = fxDiv(fxMul(cam.dirY, relX) - fxMul(cam.dirX, relY), det);
        const fx transY = fxDiv(fxMul(cam.planeX, relY) - fxMul(cam.planeY, relX), det);

        // detras de la camara, o tan cerca que la altura desbordaria
        if (transY < kMinDist) continue;

        // El nucleo centinela ocupa el doble: es el unico sprite que tiene que
        // leerse como una amenaza distinta antes de estar a rango de disparo.
        const int scale = sprites[i].kind == SPR_KIND_BOSS ? 2 : 1;
        const int size = fxFloorInt(fxDiv(fxInt(h), transY)) * scale;
        if (size <= 0) continue;

        const int screenX = fxFloorInt(fxInt(w / 2) + fxMul(fxInt(w / 2), fxDiv(transX, transY)));
        const int left = screenX - size / 2;
        const int topY = half - size / 2;

        const int x0 = std::max(0, left);
        const int x1 = std::min(w, left + size);
        const int y0 = std::max(0, topY);
        const int y1 = std::min(h, topY + size);
        if (x0 >= x1 || y0 >= y1) continue;

        // avance en la textura por pixel de pantalla, igual que en las paredes
        const fx texStep = fxDiv(fxInt(SPR_SIZE), fxInt(size));
        const uint8_t base = spriteBase(shadeLevel(transY, 0));
        const SpriteFrame& frame = spriteIsEnemy(sprites[i].kind)
            ? enemyFrame(sprites[i].kind, sprites[i].frame)
            : itemFrame(sprites[i].kind);

        for (int x = x0; x < x1; ++x) {
            // el z-buffer es lo unico que impide ver enemigos a traves de las
            // paredes (seccion 22)
            if (x < g_zbufWidth && transY >= g_wallDist[x]) continue;

            int texX = fxFloorInt(fxMul(fxInt(x - left), texStep));
            if (texX < 0 || texX >= SPR_SIZE) continue;
            const uint8_t* col = frame.px + texX;

            fx texPos = fxMul(fxInt(y0 - topY), texStep);
            for (int y = y0; y < y1; ++y, texPos += texStep) {
                int texY = fxFloorInt(texPos);
                if (texY < 0 || texY >= SPR_SIZE) continue;
                uint8_t c = col[texY * SPR_SIZE];
                if (c == 0) continue;  // transparente
                fb.setPixel(x, y, uint8_t(base + (c - 1) * SHADE_LEVELS));
            }
        }
    }
}

void renderConnectionLoss(Framebuffer& fb, int strength, int phase) {
    if (strength <= 0) return;
    const int w = fb.width(), h = fb.height();
    // Scanlines sobre TODA la imagen: el dano se lee como un CRT perdiendo
    // sincronizacion, no como dos rayas aisladas en el mundo 3D.
    const int scanStep = std::max(3, 7 - strength / 2);
    for (int y = (phase & (scanStep - 1)); y < h; y += scanStep) {
        fb.fillRect(0, y, w, 1, PAL_UI_BG);
    }

    // Vigneta muy corta: reduce el area visible igual que una television que
    // se cierra al perder senal, sin tapar por completo la accion.
    const int edge = std::min(w / 8, 2 + strength * 2);
    fb.fillRect(0, 0, edge, h, PAL_UI_BG);
    fb.fillRect(w - edge, 0, edge, h, PAL_UI_BG);

    const int bands = std::min(5 + strength * 2, 22);
    for (int i = 0; i < bands; ++i) {
        const int y = (phase * 13 + i * 29) % h;
        const int rows = 1 + ((phase + i) % 3);
        const int amount = ((phase * 7 + i * 11) % (strength * 8 + 1)) - strength * 4;
        for (int row = 0; row < rows && y + row < h; ++row) fb.shiftRow(y + row, amount);
        // Paquetes corruptos y linea de barrido: una interferencia marcada,
        // no un filtro transparente que pase desapercibido.
        const int x = (phase * 17 + i * 43) % w;
        fb.fillRect(x, y, std::min(w - x, 12 + strength * 8), 1, PAL_UI_WARN);
    }
}

void renderMinimap(Framebuffer& fb, const Maze& maze, const Player& player) {
    // La rubrica lo exige en una esquina, no al lado del mapa principal: se
    // dimensiona como fraccion de la pantalla para que ocupe lo mismo en el
    // escritorio que en los 240x160 de la GBA.
    constexpr int NUM_RAYS = 24;
    const int cell = std::max(2, fb.width() / (maze.width() * 6));
    const int margin = cell;

    auto toMapX = [&](fx cx) { return margin + ((cx * cell) >> FX_BITS); };
    auto toMapY = [&](fx cy) { return margin + ((cy * cell) >> FX_BITS); };

    fb.fillRect(margin - 1, margin - 1, maze.width() * cell + 2,
                maze.height() * cell + 2, PAL_MAP_BG);

    for (int j = 0; j < maze.height(); ++j) {
        for (int i = 0; i < maze.width(); ++i) {
            if (!maze.isWall(i, j)) continue;
            fb.fillRect(margin + i * cell, margin + j * cell, cell, cell,
                        PAL_MAP_WALL);
        }
    }

    const Camera cam = cameraOf(player);
    const int px = toMapX(player.x);
    const int py = toMapY(player.y);

    const fx cameraStep = fxDiv(2 * FX_ONE, fxInt(NUM_RAYS));
    fx cameraX = -FX_ONE;

    for (int i = 0; i < NUM_RAYS; ++i, cameraX += cameraStep) {
        fx rayX = cam.dirX + fxMul(cam.planeX, cameraX);
        fx rayY = cam.dirY + fxMul(cam.planeY, cameraX);
        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);

        // hit = pos + perpDist * rayDir, por definicion del DDA
        int hx = toMapX(player.x + fxMul(hit.perpDist, rayX));
        int hy = toMapY(player.y + fxMul(hit.perpDist, rayY));

        // linea con interpolacion, portada de Framebuffer::draw_line del Rust
        int steps = std::max({std::abs(hx - px), std::abs(hy - py), 1});
        for (int s = 0; s <= steps; ++s) {
            fb.setPixel(px + (hx - px) * s / steps, py + (hy - py) * s / steps,
                        PAL_MAP_RAY);
        }
    }

    fb.fillRect(px - 1, py - 1, 3, 3, PAL_MAP_PLAYER);
}

void renderMinimapEntities(Framebuffer& fb, const Maze& maze, const Player& player,
                           const SpriteInstance* sprites, int count) {
    const int cell = std::max(2, fb.width() / (maze.width() * 6));
    const int margin = cell;
    for (int i = 0; i < count; ++i) {
        const SpriteInstance& s = sprites[i];
        const bool enemy = spriteIsEnemy(s.kind);
        if (enemy) {
            const fx dx = s.x - player.x, dy = s.y - player.y;
            if (castRay(maze, player.x, player.y, dx, dy).perpDist <= FX_ONE) continue;
        }
        const int x = margin + ((s.x * cell) >> FX_BITS);
        const int y = margin + ((s.y * cell) >> FX_BITS);
        const uint8_t color = enemy ? spriteBase(SHADE_LEVELS - 1)
                            : (s.kind == SPR_KIND_ITEM + 3 ||
                               s.kind == SPR_KIND_ITEM + 4)
                                  ? PAL_UI_ACCENT   // llave y protocolo
                                  : PAL_UI_TEXT;
        fb.fillRect(x - 1, y - 1, 3, 3, color);
    }
}
