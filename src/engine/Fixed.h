#pragma once

#include <cstdint>

// Aritmetica de punto fijo 16.16.
//
// El ARM7TDMI de la GBA no tiene FPU: cada operacion float pasa por la libreria
// de software y cuesta cien veces mas que una entera. Todo el motor usa este
// tipo. Los float solo sobreviven donde se corren una vez al arrancar (armar la
// paleta, llenar la tabla de senos), nunca por pixel ni por rayo.

using fx = int32_t;

constexpr int FX_BITS = 16;
constexpr fx FX_ONE = fx(1) << FX_BITS;
constexpr fx FX_MASK = FX_ONE - 1;

constexpr fx fxInt(int v) { return fx(v) << FX_BITS; }

// Solo para constantes y para la carga inicial: no usar en bucles.
constexpr fx fxFloat(float v) {
    return fx(v * FX_ONE + (v < 0.0f ? -0.5f : 0.5f));
}

// Desplazamiento aritmetico: redondea hacia abajo tambien con negativos, que es
// justo el floor() que necesitan el DDA y las colisiones.
constexpr int fxFloorInt(fx v) { return int(v >> FX_BITS); }
constexpr fx fxFrac(fx v) { return v & FX_MASK; }
constexpr fx fxAbs(fx v) { return v < 0 ? -v : v; }

// smull en ARM: 32x32 -> 64 en una instruccion, no es la operacion cara.
constexpr fx fxMul(fx a, fx b) {
    return fx((int64_t(a) * int64_t(b)) >> FX_BITS);
}

// Esta si es cara, y bastante mas de lo que decia este comentario antes.
// `int64_t(a) << FX_BITS` promociona TAMBIEN el divisor a 64 bits, asi que en
// ARMv4T no compila a __aeabi_idiv sino a __aeabi_ldivmod: una division 64/64
// que, sin instruccion CLZ ni divisor por hardware, cuesta del orden de 400
// ciclos. A tres por columna son mas ciclos que el frame entero de la GBA.
// Usar fxRecip cuando el numerador sea constante.
constexpr fx fxDiv(fx a, fx b) {
    return fx((int64_t(a) << FX_BITS) / b);
}

// Reciproco 1/b en 16.16, con una sola division de 32 bits en vez de una de 64.
//
// El numerador que hace falta es 2^32, que no cabe en 32 bits; se divide 2^31 y
// se recupera el bit con el desplazamiento. Comprobado de forma exhaustiva
// contra fxDiv(FX_ONE, b) para todo b en [16, 65536]: el error maximo es de 1
// ulp, o sea 1.5e-5 celdas. Mover un pixel de altura de pared a h=160 exige un
// error relativo del 0.6%; esto esta cuatro ordenes de magnitud por debajo.
//
// b debe ser positivo. Los llamadores pasan magnitudes, no valores con signo.
inline fx fxRecip(fx b) {
    return fx((0x80000000u / uint32_t(b)) << 1);
}

// --- angulos ------------------------------------------------------------------
// Un giro completo son 65536 unidades y el tipo es uint16_t, asi que el angulo
// se envuelve solo al sumar o restar: no hace falta normalizar a [0, 2*PI).
using angle = uint16_t;

constexpr int SIN_BITS = 10;
constexpr int SIN_COUNT = 1 << SIN_BITS;  // 1024 entradas = 0.35 grados
constexpr angle ANGLE_QUARTER = 1 << 14;

// Tabla llenada una vez al arrancar. En GBA son 4 KB de EWRAM.
const fx* sinTable();

inline fx fxSin(angle a) { return sinTable()[a >> (16 - SIN_BITS)]; }
inline fx fxCos(angle a) { return fxSin(angle(a + ANGLE_QUARTER)); }

constexpr angle angleFromRad(float r) {
    return angle(int32_t(r * (65536.0f / 6.28318530718f)) & 0xFFFF);
}
