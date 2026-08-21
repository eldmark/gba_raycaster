#include "Platform.h"

#include "Debug.h"
#include "Framebuffer.h"
#include "Textures.h"

#include <gba_base.h>
#include <gba_input.h>
#include <gba_video.h>

// libgba/libtonc se incluyen solo aqui: el motor y el juego no deben saber
// que existe una GBA, igual que en escritorio no deben saber que existe SDL.

namespace {

// Modo 4: paleta indexada de 8 bits, dos paginas en VRAM para poder dibujar
// una mientras se muestra la otra. Direcciones fijas del hardware.
uint16_t* const kPage0 = reinterpret_cast<uint16_t*>(0x6000000);
uint16_t* const kPage1 = reinterpret_cast<uint16_t*>(0x600A000);

// que pagina esta MOSTRANDOSE ahora mismo (la otra es donde se escribe).
bool g_showingPage1 = false;

// El modo 4 rechaza escrituras de 8 bits: strb duplica el byte en las dos
// mitades del halfword y pisa el pixel vecino. Por eso se acumulan pares de
// indices y se escriben de una vez con strh.
inline uint16_t bgr555(uint32_t argb) {
    uint8_t r = uint8_t((argb >> 16) & 0xFF);
    uint8_t g = uint8_t((argb >> 8) & 0xFF);
    uint8_t b = uint8_t(argb & 0xFF);
    return uint16_t((b >> 3) << 10) | uint16_t((g >> 3) << 5) | uint16_t(r >> 3);
}

}  // namespace

bool Platform::init(int w, int h, const char* /*title*/) {
    width_ = w;
    height_ = h;

    REG_DISPCNT = MODE_4 | BG2_ON;

    // La paleta la construye Textures.h en ARGB de 8 bits por canal; la GBA
    // solo entiende BGR555. Se convierte una vez al arrancar, igual que
    // pintar la ventana en escritorio se hace una vez y no por pixel.
    const uint32_t* pal = palette();
    for (int i = 0; i < PALETTE_SIZE; ++i) BG_PALETTE[i] = bgr555(pal[i]);

    gbadbg::startCycleCounter();
    return true;
}

bool Platform::pollInput(Input& input) {
    input = Input{};

    // REG_KEYINPUT es activo en bajo: 0 quiere decir "pulsado". Se invierte
    // una vez aqui para que el resto del codigo piense en booleanos normales.
    uint16_t keys = uint16_t(~REG_KEYINPUT);

    input.left = (keys & KEY_LEFT) != 0;
    input.right = (keys & KEY_RIGHT) != 0;
    input.fwd = (keys & KEY_UP) != 0;
    input.back = (keys & KEY_DOWN) != 0;
    input.fire = (keys & KEY_A) != 0;
    input.start = (keys & KEY_START) != 0;
    // turn y thrust se quedan en 0: no hay raton ni stick analogico en GBA.

    // Nunca hay "cerrar la ventana": la partida solo termina apagando la
    // consola, y para entonces ya no hay bucle que siga corriendo.
    return true;
}

void Platform::present(const Framebuffer& fb) {
    // Se escribe en la pagina que NO se esta mostrando: eso es lo que hace
    // seguro dibujar mientras el LCD sigue barriendo la otra sin rasgar nada.
    uint16_t* back = g_showingPage1 ? kPage0 : kPage1;
    const uint8_t* src = fb.pixels();
    const int n = fb.width() * fb.height();

    // Pares de columnas: cada strh escribe dos pixeles de una vez, que es la
    // unica escritura que el modo 4 acepta sin duplicar el byte.
    for (int i = 0; i < n; i += 2) {
        back[i >> 1] = uint16_t(src[i]) | (uint16_t(src[i + 1]) << 8);
    }

    // Voltear la pagina solo dentro del vblank: si el bit 4 de DISPCNT cambia
    // a mitad de barrido, la mitad superior de la pantalla queda de un frame
    // y la inferior del otro.
    while (REG_VCOUNT < 160) {
    }
    if (g_showingPage1) {
        REG_DISPCNT &= ~BACKBUFFER;
    } else {
        REG_DISPCNT |= BACKBUFFER;
    }
    g_showingPage1 = !g_showingPage1;

    // Y no volver a entrar aqui hasta el siguiente vblank: sin esto, un frame
    // de logica barata volveria a escribir y voltear dentro del mismo vblank,
    // perdiendo un frame de pantalla en vez de ganar velocidad.
    while (REG_VCOUNT >= 160) {
    }
}

void Platform::setTitle(const char* /*title*/) {
    // No hay barra de titulo en la GBA. El FPS real se mide con el contador
    // de ciclos de Debug.h, no con esto.
}

void Platform::shutdown() {
    // Nada que liberar: no hay SDL, no hay heap propio de Platform.
}

unsigned long long Platform::ticksMs() const {
    // El unico reloj disponible es el contador de ciclos de hardware:
    // 16.780.000 ciclos por segundo, de sobra para integrar Player.cpp.
    return static_cast<unsigned long long>(gbadbg::cycles()) * 1000ull / 16780000ull;
}
