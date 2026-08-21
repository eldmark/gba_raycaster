#pragma once

// Medicion para la fase 3: la GBA no tiene reloj de pared, asi que la unica
// forma de saber cuantos ciclos cuesta un frame de verdad es un timer de
// hardware. TM0 cuenta un ciclo de CPU por tick (prescaler 1) y se desborda
// cada 65536 ciclos (~3,9 ms); TM1 en cascada lo extiende a 32 bits, mas que
// de sobra para un frame de 60 fps (~280.000 ciclos).
//
// El log en si sale por el puerto de depuracion que mGBA emula en
// 0x4FFF600/0x4FFF700 (no existe en hardware real, pero mGBA si lo tiene: es
// la unica forma de sacar texto de la consola sin cable de enlace ni flashcart
// con logging). Se ve corriendo mgba con -l 16 o superior.

#include <cstdint>

namespace gbadbg {

// Arranca TM0+TM1 en cascada contando ciclos de CPU. Llamar una vez en init.
void startCycleCounter();

// Ciclos de CPU transcurridos desde startCycleCounter(). 32 bits: se satura
// (no se espera medir mas de un frame de una vez).
uint32_t cycles();

// Escribe un mensaje en el log de mGBA. No hace nada en hardware real ni en
// otros emuladores: son escrituras a MMIO que mGBA reconoce y el resto
// ignora.
void log(const char* msg);

}  // namespace gbadbg
