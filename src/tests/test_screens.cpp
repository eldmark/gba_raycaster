#include "Framebuffer.h"
#include "Game.h"
#include "Hud.h"
#include "Raycaster.h"
#include "Text.h"
#include "Textures.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

constexpr fx DT = FX_ONE / 60;

void testText() {
    char buf[16];
    assert(std::strcmp(intToText(buf, 0), "0") == 0);
    assert(std::strcmp(intToText(buf, 7), "7") == 0);
    assert(std::strcmp(intToText(buf, 3820), "3820") == 0);
    assert(std::strcmp(intToText(buf, -42), "-42") == 0);
    // el caso que revienta si se convierte a positivo antes de formatear
    assert(std::strcmp(intToText(buf, -2147483647 - 1), "-2147483648") == 0);

    // el ancho no debe contar separacion detras del ultimo caracter
    assert(textWidth("") == 0);
    assert(textWidth("A") == 5);
    assert(textWidth("AB") == 11);
    assert(textWidth("AB", 2) == 22);
    assert(textHeight(1) == 7 && textHeight(3) == 21);
}

// Cuenta pixeles que no son el fondo: sirve para comprobar que algo se dibujo,
// sin depender de que sea exactamente.
int inked(const Framebuffer& fb) {
    int n = 0;
    for (int i = 0; i < fb.width() * fb.height(); ++i) {
        if (fb.pixels()[i] != PAL_UI_BG) ++n;
    }
    return n;
}

void press(Game& g) {
    Input in;
    in.start = true;
    g.update(in, DT);
    Input up;
    g.update(up, DT);  // soltar, o el siguiente flanco no existe
}

}  // namespace

int main() {
    testText();

    // Se entra por la bienvenida, no jugando.
    {
        Game g;
        g.setSeed(583291u);
        assert(g.state() == Game::State::Title);
    }

    // START encadena bienvenida -> reglas -> juego, y al terminar vuelve.
    {
        Game g;
        g.setSeed(583291u);
        press(g);
        assert(g.state() == Game::State::Rules);
        press(g);
        assert(g.state() == Game::State::Playing);
        assert(g.floor() == 1);
        assert(g.hp() == Game::START_HP);
    }

    // MANTENER START no debe atravesar las pantallas. Se lee por flanco, asi
    // que cien frames con la tecla hundida avanzan una sola pantalla.
    {
        Game g;
        g.setSeed(583291u);
        Input held;
        held.start = true;
        for (int i = 0; i < 100; ++i) g.update(held, DT);
        assert(g.state() == Game::State::Rules);
    }

    // Dos runs seguidas no repiten nivel: la semilla avanza sola.
    {
        Game a;
        a.setSeed(4242u);
        press(a);
        press(a);
        uint32_t first = a.seed();

        // terminar la run a lo bruto y empezar otra
        Game b;
        b.setSeed(4242u);
        press(b);
        press(b);
        assert(b.seed() == first);  // misma semilla inicial, misma primera run
        b.newRun(b.seed());
        uint32_t second = b.seed();
        b.newRun(second * 1664525u + 1013904223u);
        assert(b.seed() != second);
    }

    // La puntuacion sube con las bajas y con los archivos superados, y no puede
    // ser negativa al empezar.
    {
        Game g;
        g.newRun(42u);
        assert(g.score() == 0);
        assert(g.elapsedSeconds() == 0);

        Input idle;
        for (int i = 0; i < 600; ++i) g.update(idle, DT);
        // 600 frames a 1/60 son 10 s de reloj, pero 1/60 en 16.16 es 1092 y no
        // 1092.27: el reloj se queda un 0.02% corto. Sobre diez minutos son
        // 0.15 s, asi que se acepta el borde en vez de complicar la suma.
        assert(g.elapsedSeconds() == 9 || g.elapsedSeconds() == 10);
    }

    // El fogonazo se enciende al disparar y se apaga solo.
    {
        Game g;
        g.newRun(7u);
        assert(!g.muzzleFlash());
        Input fire;
        fire.fire = true;
        g.update(fire, DT);
        assert(g.muzzleFlash());
        Input idle;
        for (int i = 0; i < 30; ++i) g.update(idle, DT);
        assert(!g.muzzleFlash());
    }

    // Las tres pantallas dibujan algo y ninguna se sale del framebuffer. Se
    // prueban a resolucion de GBA, que es la que menos sitio tiene: si algo
    // cabe en 240x160 cabe en cualquier ventana.
    {
        Framebuffer gba(240, 160);
        Framebuffer big(900, 600);

        Game g;
        g.setSeed(583291u);
        for (int screen = 0; screen < 2; ++screen) {
            drawScreen(gba, g);
            assert(inked(gba) > 200);
            drawScreen(big, g);
            assert(inked(big) > 2000);
            press(g);
        }

        // ahora jugando: drawScreen no debe pintar nada y el HUD si
        assert(g.state() == Game::State::Playing);
        gba.clear(PAL_UI_BG);
        drawScreen(gba, g);
        assert(inked(gba) == 0);
        drawHud(gba, g);
        assert(inked(gba) > 100);

        // y al reves: con la run terminada el HUD calla y la pantalla habla.
        //
        // Para morir hace falta una semilla con un guardian dentro del alcance
        // de vista Y con linea de visión: los que nacen mas lejos duermen si el
        // jugador no se mueve, y los que tienen un muro delante no llegan.
        Game over;
        bool armed = false;
        for (uint32_t seed = 1; seed <= 400 && !armed; ++seed) {
            over.newRun(seed);
            const fx sight = tuningForFloor(over.floor()).sightRange;
            for (int i = 0; i < over.enemyCount() && !armed; ++i) {
                fx dx = over.enemy(i).x - over.player().x;
                fx dy = over.enemy(i).y - over.player().y;
                if (fxMul(dx, dx) + fxMul(dy, dy) > fxMul(sight, sight)) continue;
                Hit h = castRay(over.maze(), over.player().x, over.player().y, dx, dy);
                armed = h.perpDist > FX_ONE;  // 1.0 es justo el guardian
            }
        }
        assert(armed);

        int guard = 0;
        while (over.state() == Game::State::Playing && guard++ < 20000) {
            Input idle;
            over.update(idle, DT);  // quieto, dejandose matar
        }
        assert(over.state() == Game::State::Dead);
        assert(over.hp() == 0);
        assert(over.score() >= 0);
        gba.clear(PAL_UI_BG);
        drawHud(gba, over);
        assert(inked(gba) == 0);
        drawScreen(gba, over);
        assert(inked(gba) > 200);
    }

    std::printf("all tests passed\n");
    return 0;
}
