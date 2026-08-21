#include "Fixed.h"
#include "Maze.h"
#include "Player.h"
#include "Raycaster.h"
#include "Renderer.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

const char* kMazePath = "test_maze.txt";

// Tolerancia de una celda: 1/256. El 16.16 no da exacto lo que daba el float,
// pero un error de mas de 1/256 de celda ya se veria en pantalla.
constexpr fx kEps = FX_ONE / 256;

void writeMaze() {
    FILE* f = std::fopen(kMazePath, "w");
    assert(f);
    std::fputs("+++\n+ +\n+++\n", f);
    std::fclose(f);
}

// Un rayo desde el centro de la celda libre choca a media celda en los 4 ejes.
void checkAxis(const Maze& maze, fx dirX, fx dirY, int expectedSide) {
    Hit h = castRay(maze, fxFloat(1.5f), fxFloat(1.5f), dirX, dirY);
    assert(fxAbs(h.perpDist - FX_ONE / 2) < kEps);
    assert(h.impact == '+');
    assert(h.side == expectedSide);
    assert(h.wallX >= 0 && h.wallX < FX_ONE);
}

void testFixed() {
    assert(fxFloorInt(fxFloat(2.75f)) == 2);
    // floor y no truncado: es de lo que dependen el DDA y las colisiones
    assert(fxFloorInt(fxFloat(-0.5f)) == -1);
    assert(fxFloorInt(fxFloat(-2.75f)) == -3);
    assert(fxFrac(fxFloat(-0.25f)) == fxFloat(0.75f));

    assert(fxAbs(fxMul(fxFloat(2.5f), fxFloat(4.0f)) - fxInt(10)) < kEps);
    assert(fxAbs(fxMul(fxFloat(-2.5f), fxFloat(4.0f)) + fxInt(10)) < kEps);
    assert(fxAbs(fxDiv(fxInt(10), fxFloat(2.5f)) - fxInt(4)) < kEps);
    assert(fxAbs(fxDiv(fxInt(-10), fxFloat(2.5f)) + fxInt(4)) < kEps);

    // La tabla de senos debe coincidir con sin/cos reales en toda la vuelta, o
    // el jugador se mueve en una direccion distinta a la que mira.
    for (int i = 0; i < 512; ++i) {
        angle a = angle(i * (65536 / 512));
        double rad = 2.0 * M_PI * i / 512.0;
        assert(std::fabs(double(fxSin(a)) / FX_ONE - std::sin(rad)) < 0.01);
        assert(std::fabs(double(fxCos(a)) / FX_ONE - std::cos(rad)) < 0.01);
    }
    // el angulo es uint16_t: debe envolverse solo, sin normalizar a mano
    assert(fxSin(angle(70000)) == fxSin(angle(70000 - 65536)));
}

}  // namespace

int main() {
    testFixed();

    writeMaze();
    Maze maze;
    bool ok = maze.load(kMazePath);
    std::remove(kMazePath);
    assert(ok);
    assert(maze.width() == 3 && maze.height() == 3);

    // Port del test de laberinto/src/caster.rs: 20px con BLOCK_SIZE=40 = 0.5 celdas.
    checkAxis(maze, FX_ONE, 0, 0);
    checkAxis(maze, -FX_ONE, 0, 0);
    checkAxis(maze, 0, FX_ONE, 1);
    checkAxis(maze, 0, -FX_ONE, 1);

    // Diagonal a 45 grados: distancia finita y positiva.
    const fx k = fxFloat(0.70710678f);
    Hit d = castRay(maze, fxFloat(1.5f), fxFloat(1.5f), k, k);
    assert(d.perpDist > 0);
    assert(d.impact == '+');
    assert(d.wallX >= 0 && d.wallX < FX_ONE);

    // Barrido completo con la tabla de senos: wallX siempre en [0,1), la
    // distancia siempre positiva y nada desborda el 16.16.
    for (int i = 0; i < SIN_COUNT; ++i) {
        angle a = angle(i * (65536 / SIN_COUNT));
        Hit h = castRay(maze, fxFloat(1.5f), fxFloat(1.5f), fxCos(a), fxSin(a));
        assert(h.wallX >= 0 && h.wallX < FX_ONE);
        assert(h.perpDist >= kMinDist);
        // dentro de una celda de 1x1 nunca se puede estar mas lejos que la
        // diagonal; si se pasa, algo desbordo
        assert(h.perpDist < fxInt(2));
    }

    // Arrancar dentro de una pared no puede colgar ni devolver distancia <= 0.
    Hit inside = castRay(maze, FX_ONE / 2, FX_ONE / 2, FX_ONE, 0);
    assert(inside.perpDist >= kMinDist);

    // Sin ojo de pez: mirando de frente a una pared plana, todas las columnas
    // deben devolver la MISMA perpDist, o la pared se ve curva. Es el unico
    // chequeo que atrapa una correccion de coseno faltante o aplicada de mas.
    // Se replica aqui la base de camara de Renderer.cpp a proposito.
    {
        fx dirX = 0, dirY = -FX_ONE;              // mirando hacia arriba (-Y)
        fx half = fxFloat(0.57735027f);           // tan(60 grados / 2)
        fx planeX = -fxMul(dirY, half), planeY = fxMul(dirX, half);
        for (int x = 0; x < 64; ++x) {
            fx cameraX = fxDiv(fxInt(2 * x), fxInt(64)) - FX_ONE;
            Hit h = castRay(maze, fxFloat(1.5f), fxFloat(1.5f),
                            dirX + fxMul(planeX, cameraX),
                            dirY + fxMul(planeY, cameraX));
            assert(fxAbs(h.perpDist - FX_ONE / 2) < kEps);
        }
    }

    assert(!collides(maze, fxFloat(1.5f), fxFloat(1.5f)));
    assert(collides(maze, fxFloat(1.5f), fxFloat(0.5f)));  // pared de arriba
    assert(collides(maze, fxInt(-1), fxFloat(1.5f)));      // fuera del mapa

    // El jugador no puede atravesar una pared por mucho que empuje: requisito
    // obligatorio de la entrega, no un extra.
    {
        Player p{fxFloat(1.5f), fxFloat(1.5f), angleFromRad(-1.5707963f),
                 fxFloat(0.57735027f)};  // mirando a -Y, contra la pared
        Input in;
        in.fwd = true;
        for (int i = 0; i < 600; ++i) updatePlayer(p, in, maze, FX_ONE / 60);
        assert(!collides(maze, p.x, p.y));
        assert(p.y > FX_ONE);  // sigue dentro de la celda libre
    }

    // --- entrada analogica ----------------------------------------------------
    // El giro del raton NO se escala por dt: el raton entrega un
    // desplazamiento ya hecho, no una velocidad, y multiplicarlo por el tiempo
    // del frame ataria la sensibilidad a los FPS.
    {
        Player fast{fxFloat(1.5f), fxFloat(1.5f), 0, fxFloat(0.57735027f)};
        Player slow = fast;
        Input in;
        in.turn = 3000;
        updatePlayer(fast, in, maze, FX_ONE / 240);  // frame corto
        updatePlayer(slow, in, maze, FX_ONE / 30);   // frame largo
        assert(fast.a == slow.a);
        assert(fast.a == angle(3000));
    }

    // El empuje analogico manda sobre las teclas y es proporcional: a medio
    // stick se recorre la mitad que con la tecla a fondo.
    {
        Player full{fxFloat(1.5f), fxFloat(1.5f), 0, fxFloat(0.57735027f)};
        Player half = full;
        Input key;
        key.fwd = true;
        Input stick;
        stick.fwd = true;  // la tecla esta pulsada y aun asi manda el stick
        stick.thrust = FX_ONE / 2;
        updatePlayer(full, key, maze, FX_ONE / 60);
        updatePlayer(half, stick, maze, FX_ONE / 60);

        const fx moved = full.x - fxFloat(1.5f);
        const fx movedHalf = half.x - fxFloat(1.5f);
        assert(moved > 0 && movedHalf > 0);
        assert(fxAbs(movedHalf * 2 - moved) < 4);  // redondeo del punto fijo
    }

    // Y el stick hacia atras retrocede, aunque la tecla de avanzar siga
    // pulsada: si no, un mando y un teclado a la vez se pelearian.
    {
        Player p{fxFloat(1.5f), fxFloat(1.5f), 0, fxFloat(0.57735027f)};
        Input in;
        in.fwd = true;
        in.thrust = -FX_ONE;
        updatePlayer(p, in, maze, FX_ONE / 60);
        assert(p.x < fxFloat(1.5f));
    }

    // La inclinacion de la vista mueve el horizonte y se queda topada. Mirar
    // arriba lo BAJA: eso es lo que descubre mas cielo.
    {
        Player p{fxFloat(1.5f), fxFloat(1.5f), 0, fxFloat(0.57735027f)};
        assert(horizonY(160, p) == 80);  // sin inclinar, el horizonte al centro

        Input up;
        up.look = fxFloat(0.25f);
        updatePlayer(p, up, maze, FX_ONE / 60);
        assert(p.pitch > 0);
        assert(horizonY(160, p) > 80);

        // Mantenerlo a fondo no se sale de la pantalla: el tope de Player.cpp
        // corta antes, y horizonY recorta lo que quede.
        for (int i = 0; i < 200; ++i) updatePlayer(p, up, maze, FX_ONE / 60);
        const int top = horizonY(160, p);
        assert(top > 80 && top < 160);

        Input down;
        down.look = -fxFloat(0.25f);
        for (int i = 0; i < 400; ++i) updatePlayer(p, down, maze, FX_ONE / 60);
        const int bottom = horizonY(160, p);
        assert(bottom < 80 && bottom > 0);
    }

    std::printf("all tests passed\n");
    return 0;
}
