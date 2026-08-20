#include "Fixed.h"

#include <cmath>

// Se llena en el primer uso con float: corre una sola vez al arrancar, no en el
// bucle de render. A partir de ahi el motor no vuelve a tocar un float.
const fx* sinTable() {
    static fx table[SIN_COUNT];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < SIN_COUNT; ++i) {
            double a = 2.0 * M_PI * i / SIN_COUNT;
            table[i] = fx(std::lround(std::sin(a) * FX_ONE));
        }
        built = true;
    }
    return table;
}
