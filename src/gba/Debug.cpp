#include "Debug.h"

#include <gba_base.h>
#include <gba_timers.h>
#include <cstring>

namespace {
// Puerto de depuracion de mGBA (mgba.io/2015/06/26/mgba-0.3, seccion
// "Debugging Support"). No es hardware de GBA real: escribir aqui en una
// consola de verdad simplemente no hace nada, porque esas direcciones caen en
// una zona de I/O sin mapear.
volatile uint16_t& regDebugEnable() { return *reinterpret_cast<volatile uint16_t*>(0x4FFF780); }
volatile uint16_t& regDebugFlags() { return *reinterpret_cast<volatile uint16_t*>(0x4FFF700); }
char* regDebugString() { return reinterpret_cast<char*>(0x4FFF600); }
}  // namespace

namespace gbadbg {

void startCycleCounter() {
    REG_TM0CNT_H = 0;
    REG_TM1CNT_H = 0;
    REG_TM0CNT_L = 0;
    REG_TM1CNT_L = 0;
    REG_TM0CNT_H = TIMER_START;                // prescaler 1: un tick por ciclo de CPU
    REG_TM1CNT_H = TIMER_START | TIMER_COUNT;   // cascada: cuenta los desbordes de TM0
}

uint32_t cycles() {
    // TM1 es la mitad alta; hay que leer TM0 dos veces por si acaso cascadea
    // justo entre medias (mismo truco que leer un contador de 64 bits en dos
    // mitades de 32).
    uint16_t hi1 = REG_TM1CNT_L;
    uint16_t lo = REG_TM0CNT_L;
    uint16_t hi2 = REG_TM1CNT_L;
    if (hi2 != hi1) lo = REG_TM0CNT_L;  // desbordo justo ahora: releer la mitad baja
    return (uint32_t(hi2) << 16) | lo;
}

void log(const char* msg) {
    regDebugEnable() = 0xC0DE;
    size_t n = std::strlen(msg);
    if (n > 255) n = 255;  // el buffer de mGBA son 256 bytes
    std::memcpy(regDebugString(), msg, n);
    regDebugString()[n] = '\0';
    regDebugFlags() = 3 | 0x100;  // nivel INFO, bit 8 = enviar
}

}  // namespace gbadbg
