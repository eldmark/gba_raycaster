#include "Renderer.h"

#include <algorithm>

#include "Fixed.h"

#include "Framebuffer.h"
#include "Maze.h"
#include "Player.h"
#include "Raycaster.h"
#include "Textures.h"

// Instrumental de la fase 4: reparte el coste de renderWorld entre el fondo,
// el DDA y el bucle de texturas. Solo existe en la ROM del target "profile"
// de la GBA; en escritorio GBA_PROFILE nunca esta definido y de aqui no queda
// ni una instruccion.
#ifdef GBA_PROFILE
#include "../gba/Debug.h"
extern "C" volatile uint32_t g_bgCycles;
volatile uint32_t g_bgCycles = 0;
extern "C" volatile uint32_t g_rayCycles;
volatile uint32_t g_rayCycles = 0;
extern "C" volatile uint32_t g_texCycles;
volatile uint32_t g_texCycles = 0;
extern "C" volatile uint32_t g_mmBgCycles;
volatile uint32_t g_mmBgCycles = 0;
extern "C" volatile uint32_t g_mmCellCycles;
volatile uint32_t g_mmCellCycles = 0;
extern "C" volatile uint32_t g_mmRayCycles;
volatile uint32_t g_mmRayCycles = 0;
extern "C" volatile uint32_t g_mmEntCycles;
volatile uint32_t g_mmEntCycles = 0;
#define PROF_BEGIN() const uint32_t profStart = gbadbg::cycles()
#define PROF_ADD(acc) (acc) += gbadbg::cycles() - profStart
#else
#define PROF_BEGIN() ((void)0)
#define PROF_ADD(acc) ((void)0)
#endif

namespace {

// El sombreado se apaga linealmente hasta 12,5 celdas, que son los 500 px de
// mundo del prototipo divididos por sus 40 px de celda. Se guarda el reciproco
// para que sombrear sea una multiplicacion y no una division.
constexpr fx INV_SHADE_RANGE = fxFloat(1.0f / 12.5f);
constexpr fx MIN_LIGHT = fxFloat(0.25f);
constexpr fx SIDE_LIGHT = fxFloat(0.7f);

// Lineas de rejilla del suelo, contadas en medias celdas. Las enteras solas
// dejan vacio el campo cercano, porque la primera cae justo en el borde de
// abajo de la pantalla; mas alla de estas, todas caen sobre la misma fila.
constexpr int GRID_STEPS = 20;

// Escribir dos pixeles de una vez pide alcanzar un array de uint8_t con un
// lvalue de 16 bits, y eso es UB por aliasing estricto. may_alias es la forma
// que GCC documenta para pedirlo sin mentirle al optimizador. std::memcpy, que
// seria la alternativa portable, NO se convirtio en un strh con este toolchain:
// el bucle de texels se fue de 130.976 a 557.592 ciclos.
typedef uint16_t u16_alias __attribute__((may_alias));

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

#ifdef GBA_PROFILE
    g_bgCycles = 0;
    g_rayCycles = 0;
    g_texCycles = 0;
    { PROF_BEGIN();
#endif

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

#ifdef GBA_PROFILE
    PROF_ADD(g_bgCycles); }
#endif

    // Un rayo cada RAY_STEP columnas. En los 240 pixeles de la consola se traza
    // la mitad y cada resultado pinta dos columnas: el DDA y toda la
    // preparacion por columna --el reciproco de la altura, la tabla de colores,
    // el espejado de la cara-- se pagan una vez en vez de dos. El coste es
    // grano horizontal del doble, que es lo que hacian los raycasters de
    // consola de la epoca.
    //
    // Se decide por el ANCHO y no con un #ifdef de plataforma a proposito: las
    // pruebas de frames.ref renderizan a 240x160, que es la resolucion de la
    // consola, y existen justamente para vigilar lo que la consola dibuja. Con
    // un #ifdef dejarian de describirla. La ventana de escritorio, a 900, sigue
    // trazando un rayo por columna.
    const int rayStep = (w <= 320) ? 2 : 1;

    // cameraX barre [-1, 1) de a pasos iguales. Incremental para no pagar una
    // division por columna: la unica que queda es la de la altura.
    const fx cameraStep = fxDiv(2 * FX_ONE, fxInt(w)) * rayStep;
    fx cameraX = -FX_ONE;

    // TEX_SIZE/h es constante, asi que el avance en textura sale de una
    // multiplicacion por perpDist en vez de dividir entre la altura proyectada.
    const fx texPerDist = fxDiv(fxInt(TEX_SIZE), fxInt(h));

    for (int x = 0; x < w; x += rayStep, cameraX += cameraStep) {
        // Las columnas que cubre este rayo. La ultima puede quedar corta si w
        // no es multiplo de rayStep.
        const int span = std::min(rayStep, w - x);

        fx rayX = cam.dirX + fxMul(cam.planeX, cameraX);
        fx rayY = cam.dirY + fxMul(cam.planeY, cameraX);

#ifdef GBA_PROFILE
        PROF_BEGIN();
#endif
        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);
#ifdef GBA_PROFILE
        PROF_ADD(g_rayCycles);
#endif

        // Altura proyectada. La tercera division por columna, tambien por
        // reciproco en vez de dividir de verdad. perpDist
        // esta acotada por abajo por kMinDist, asi que el producto no desborda.
        fx stakeHeight = fxMul(fxInt(h), fxRecip(hit.perpDist));
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
#ifdef GBA_PROFILE
        { PROF_BEGIN();
#endif
        // Puntero crudo en vez de setPixel. Este bucle es EL punto caliente
        // del motor: en GBA costaba 2.398.152 ciclos de los 5.898.231 del
        // frame entero, unos 100 por pixel. La mayoria no era el pixel, era
        // la llamada: setPixel vive en otra unidad de traduccion, no se puede
        // inlinear sin LTO, y ademas comprueba cuatro limites y multiplica
        // y*width por cada uno de los ~24.000 pixeles de pared. Todo eso se
        // ejecuta desde la ROM, que en la consola tiene esperas.
        //
        // Aqui los cuatro limites ya estan garantizados: top viene de un
        // max(0,...), bottom de un min(h,...) y x es el indice del bucle de
        // columnas. Avanzar el destino de fila en fila es una suma.
        uint8_t* dst = fb.pixels() + size_t(top) * size_t(w) + size_t(x);
        // El caso de dos columnas sale del bucle: meter un for interno de
        // longitud variable aqui costo mas que todo lo que ahorraba el medio
        // rayo -- el bucle de texels paso de 267.756 a 682.040 ciclos.
        //
        // Las dos columnas se escriben como un halfword. La direccion es par
        // (el ancho es 240, x avanza de dos en dos y el buffer esta alineado),
        // y el bus de EWRAM es de 16 bits: un strh cuesta un acceso donde dos
        // strb costaban dos.
        if (span == 2) {
            for (int y = top; y < bottom; ++y, texPos += step, dst += w) {
                int texY = fxFloorInt(texPos) & (TEX_SIZE - 1);
                const uint8_t c = colBase[col[texY * TEX_SIZE]];
                *reinterpret_cast<u16_alias*>(dst) =
                    uint16_t(uint16_t(c) | uint16_t(c) << 8);
            }
        } else {
            for (int y = top; y < bottom; ++y, texPos += step, dst += w) {
                int texY = fxFloorInt(texPos) & (TEX_SIZE - 1);
                *dst = colBase[col[texY * TEX_SIZE]];
            }
        }
#ifdef GBA_PROFILE
        PROF_ADD(g_texCycles); }
#endif

        // El z-buffer se llena para TODAS las columnas del grupo: renderSprites
        // lo consulta columna a columna y una sin rellenar dejaria pasar un
        // sprite por delante de la pared.
        for (int k = 0; k < span && x + k < MAX_SCREEN_W; ++k) {
            g_wallDist[x + k] = hit.perpDist;
        }
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

        // transY solo esta acotada por kMinDist, asi que un enemigo casi
        // perpendicular a la vista da un cociente de miles y el fxMul
        // desbordaba el int32: screenX salia arbitrario y podia aterrizar
        // dentro de la pantalla. La cuenta se hace en 64 bits y solo se recorta
        // el resultado, de modo que todo caso que antes NO desbordaba sale
        // identico; el sprite mas grande posible mide h/kMinDist, asi que a
        // 100000 pixeles del centro esta fuera de pantalla con enorme margen.
        const fx ratio = fxDiv(transX, transY);
        int64_t sx = (int64_t(fxInt(w / 2)) +
                      ((int64_t(fxInt(w / 2)) * int64_t(ratio)) >> FX_BITS)) >> FX_BITS;
        if (sx > 100000) sx = 100000;
        if (sx < -100000) sx = -100000;
        const int screenX = int(sx);
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

// Lado en pixeles de una celda del minimapa. Lo usan renderMinimap y
// renderMinimapEntities, y tiene que dar lo MISMO en las dos o los marcadores
// se despegan del mapa; por eso vive aqui una sola vez.
//
// Se dimensiona contra la ALTURA de la pantalla y no contra el ancho: el mapa
// es cuadrado y la altura es el lado corto, asi que atarlo al ancho hacia que
// en los 240x160 de la consola el minimapa creciera hasta 96x96 pixeles -- mas
// de la mitad del alto de la pantalla-- mientras en el escritorio se veia
// pequeno. Un cuarto de la altura es lo que cabe en una esquina sin tapar el
// juego. En escritorio da exactamente el mismo tamano que antes para todos los
// tamanos de mapa que genera Level.cpp (24 a 48); en GBA lo deja a la mitad.
int minimapCell(const Framebuffer& fb, const Maze& maze) {
    return std::max(1, (fb.height() / 4) / maze.height());
}

void renderMinimap(Framebuffer& fb, const Maze& maze, const Player& player) {
    // La rubrica lo exige en una esquina, no al lado del mapa principal.
    constexpr int NUM_RAYS = 24;
    const int cell = minimapCell(fb, maze);
    const int margin = cell;

    auto toMapX = [&](fx cx) { return margin + ((cx * cell) >> FX_BITS); };
    auto toMapY = [&](fx cy) { return margin + ((cy * cell) >> FX_BITS); };

#ifdef GBA_PROFILE
    { PROF_BEGIN();
#endif
    fb.fillRect(margin - 1, margin - 1, maze.width() * cell + 2,
                maze.height() * cell + 2, PAL_MAP_BG);
#ifdef GBA_PROFILE
    PROF_ADD(g_mmBgCycles); }
    { PROF_BEGIN();
#endif

    // Una llamada a fillRect por celda de muro costaba 234.530 ciclos por
    // frame, el 63% del minimapa entero: unos 300 por celda, casi todo en la
    // llamada y su recorte, para pintar un cuadrado de 1 o 5 pixeles de lado.
    // Aqui se escribe por puntero, y el recorte se hace UNA vez acotando los
    // limites del bucle en vez de por celda.
    uint8_t* const px0 = fb.pixels();
    const int fbw = fb.width();
    const int maxI = std::min(maze.width(), (fbw - margin) / cell);
    const int maxJ = std::min(maze.height(), (fb.height() - margin) / cell);
    for (int j = 0; j < maxJ; ++j) {
        uint8_t* row = px0 + size_t(margin + j * cell) * size_t(fbw) + size_t(margin);
        // cell==1 es el caso de la consola, y ahi los dos bucles de abajo
        // montaban su armazon entero para escribir un solo byte.
        if (cell == 1) {
            for (int i = 0; i < maxI; ++i) {
                if (maze.isWall(i, j)) row[i] = PAL_MAP_WALL;
            }
            continue;
        }
        for (int i = 0; i < maxI; ++i) {
            if (!maze.isWall(i, j)) continue;
            uint8_t* p = row + i * cell;
            for (int cy = 0; cy < cell; ++cy, p += fbw) {
                for (int cx = 0; cx < cell; ++cx) p[cx] = PAL_MAP_WALL;
            }
        }
    }
#ifdef GBA_PROFILE
    PROF_ADD(g_mmCellCycles); }
#endif

    const Camera cam = cameraOf(player);
    const int px = toMapX(player.x);
    const int py = toMapY(player.y);

    const fx cameraStep = fxDiv(2 * FX_ONE, fxInt(NUM_RAYS));
    fx cameraX = -FX_ONE;

#ifdef GBA_PROFILE
    { PROF_BEGIN();
#endif
    for (int i = 0; i < NUM_RAYS; ++i, cameraX += cameraStep) {
        fx rayX = cam.dirX + fxMul(cam.planeX, cameraX);
        fx rayY = cam.dirY + fxMul(cam.planeY, cameraX);
        Hit hit = castRay(maze, player.x, player.y, rayX, rayY);

        // hit = pos + perpDist * rayDir, por definicion del DDA
        int hx = toMapX(player.x + fxMul(hit.perpDist, rayX));
        int hy = toMapY(player.y + fxMul(hit.perpDist, rayY));

        // Linea con interpolacion. La version de la que sale esto hacia dos
        // divisiones enteras POR PIXEL --(hx-px)*s/steps y su gemela-- y el
        // ARM7TDMI no tiene division: cada una es una rutina de software. Aqui
        // se paga un reciproco y dos multiplicaciones por RAYO, y el avance
        // por pixel es una suma.
        int steps = std::max({std::abs(hx - px), std::abs(hy - py), 1});
        const fx invSteps = fxRecip(fxInt(steps));
        const fx stepX = fxMul(fxInt(hx - px), invSteps);
        const fx stepY = fxMul(fxInt(hy - py), invSteps);
        fx lx = fxInt(px), ly = fxInt(py);
        for (int s = 0; s <= steps; ++s, lx += stepX, ly += stepY) {
            fb.setPixel(fxFloorInt(lx), fxFloorInt(ly), PAL_MAP_RAY);
        }
    }

#ifdef GBA_PROFILE
    PROF_ADD(g_mmRayCycles); }
#endif

    fb.fillRect(px - 1, py - 1, 3, 3, PAL_MAP_PLAYER);
}

void renderMinimapEntities(Framebuffer& fb, const Maze& maze, const Player& player,
                           const SpriteInstance* sprites, int count) {
    const int cell = minimapCell(fb, maze);
    const int margin = cell;
#ifdef GBA_PROFILE
    PROF_BEGIN();
#endif
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
#ifdef GBA_PROFILE
    PROF_ADD(g_mmEntCycles);
#endif
}
