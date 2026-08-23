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
    // TM2+TM3 y no TM0+TM1: el reloj de muestreo de DirectSound (src/gba/Audio.cpp)
    // solo puede colgar de TM0 o TM1 -- lo elige un bit de SOUNDCNT_H y no hay
    // mas opciones-- asi que el contador de ciclos, que puede vivir en
    // cualquiera, se aparta a los dos de arriba.
    REG_TM2CNT_H = 0;
    REG_TM3CNT_H = 0;
    REG_TM2CNT_L = 0;
    REG_TM3CNT_L = 0;
    REG_TM2CNT_H = TIMER_START;                // prescaler 1: un tick por ciclo de CPU
    REG_TM3CNT_H = TIMER_START | TIMER_COUNT;   // cascada: cuenta los desbordes de TM2
}

uint32_t cycles() {
    // TM3 es la mitad alta; hay que leer TM2 dos veces por si acaso cascadea
    // justo entre medias (mismo truco que leer un contador de 64 bits en dos
    // mitades de 32).
    uint16_t hi1 = REG_TM3CNT_L;
    uint16_t lo = REG_TM2CNT_L;
    uint16_t hi2 = REG_TM3CNT_L;
    if (hi2 != hi1) lo = REG_TM2CNT_L;  // desbordo justo ahora: releer la mitad baja
    return (uint32_t(hi2) << 16) | lo;
}

void log(const char* msg) {
    regDebugEnable() = 0xC0DE;
    size_t n = std::strlen(msg);
    if (n > 255) n = 255;  // el buffer de mGBA son 256 bytes
    std::memcpy(regDebugString(), msg, n);
    regDebugString()[n] = '\0';
    // Nivel WARN (2) y no INFO (3): mGBA registra CADA transferencia de DMA a
    // nivel INFO, y con el audio alimentando dos FIFO por VBlank eso son miles
    // de lineas por segundo que ahogan la medida y frenan el emulador. A nivel
    // WARN se lee con "mgba -l 4" y solo sale lo nuestro.
    regDebugFlags() = 2 | 0x100;  // nivel WARN, bit 8 = enviar
}

}  // namespace gbadbg
