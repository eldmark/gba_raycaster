#pragma once

#include <cstdint>

// Xorshift32: el generador mas barato que pasa por aleatorio para lo que hace
// falta aca. Una multiplicacion no, tres shifts y tres xor si, que es justo lo
// que el ARM7TDMI hace rapido.
//
// Con estado explicito a proposito: la generacion de niveles tiene que ser
// reproducible a partir de la seed (seccion 14 del PROJECT.md), y un rand()
// global con estado escondido no lo garantiza.
class Random {
public:
    explicit Random(uint32_t seed) : state_(seed ? seed : 0x1234567u) {}

    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    // Entero en [lo, hi]. El modulo sesga las ultimas combinaciones, pero con
    // rangos de dos digitos el sesgo es inmedible y evita un bucle de rechazo.
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + int(next() % uint32_t(hi - lo + 1));
    }

    // Probabilidad en porcentaje.
    bool chance(int percent) { return range(1, 100) <= percent; }

private:
    uint32_t state_;
};
