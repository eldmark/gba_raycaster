#include "Platform.h"

#include "Debug.h"
#include "Framebuffer.h"
#include "Textures.h"

#include <gba_base.h>
#include <gba_dma.h>
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

    // Esperas del bus del cartucho y prefetch. Al arrancar, la consola deja
    // WAITCNT en 0: 4+2 ciclos de espera por acceso a la ROM y sin buffer de
    // prefetch. Como TODO el codigo se ejecuta desde la ROM, eso es un impuesto
    // sobre cada instruccion del motor.
    //
    // 0x4317: WS0 en 3/1, SRAM en 3, y el bit 14 enciende el prefetch, que va
    // adelantando instrucciones mientras la CPU no usa el bus. Son los valores
    // que aceptan los cartuchos comerciales; libgba no lo toca.
    *reinterpret_cast<volatile uint16_t*>(0x4000204) = 0x4317;

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
    // SELECT congela, START reanuda. Dos botones y no uno que alterna: a los
    // 12-15 fps que da la consola un pulso llega a leerse en dos frames
    // seguidos, y con un solo boton eso entra y sale de la pausa en el mismo
    // toque.
    input.pause = (keys & KEY_SELECT) != 0;
    input.resume = (keys & KEY_START) != 0;
    // turn y thrust se quedan en 0: no hay raton ni stick analogico en GBA.

    // Nunca hay "cerrar la ventana": la partida solo termina apagando la
    // consola, y para entonces ya no hay bucle que siga corriendo.
    return true;
}

#ifdef GBA_PROFILE
// Desglose de present(): copiar EWRAM->VRAM y esperar al vblank son dos costes
// distintos y solo uno se puede optimizar. Medirlos juntos escondia cual era
// cual. Nombre plano y extern "C" para poder leerlos tambien desde gdb.
extern "C" volatile uint32_t g_copyCycles;
volatile uint32_t g_copyCycles = 0;
extern "C" volatile uint32_t g_vblankCycles;
volatile uint32_t g_vblankCycles = 0;
#endif

void Platform::present(const Framebuffer& fb) {
    // Se escribe en la pagina que NO se esta mostrando: eso es lo que hace
    // seguro dibujar mientras el LCD sigue barriendo la otra sin rasgar nada.
    uint16_t* back = g_showingPage1 ? kPage0 : kPage1;
    const uint8_t* src = fb.pixels();
    const int n = fb.width() * fb.height();

#ifdef GBA_PROFILE
    const uint32_t beforeCopy = gbadbg::cycles();
#endif

    // DMA3 en palabras de 32 bits. Antes esto era un bucle de la CPU que leia
    // dos bytes de EWRAM y los juntaba en un strh, y costaba 902.501 ciclos:
    // el bus de EWRAM es de 16 bits, asi que cada uno de los 38.400 bytes se
    // pagaba a precio de acceso suelto. El DMA lee de a 32 bits y no ejecuta
    // instrucciones por el camino.
    //
    // Sigue sin haber ninguna escritura de 8 bits, que es lo que el modo 4
    // rechaza: el DMA transfiere palabras enteras.
    DMA3COPY(src, back, DMA_ENABLE | DMA32 | uint32_t(n / 4));

#ifdef GBA_PROFILE
    g_copyCycles = gbadbg::cycles() - beforeCopy;
    const uint32_t beforeVblank = gbadbg::cycles();
#endif

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

#ifdef GBA_PROFILE
    g_vblankCycles = gbadbg::cycles() - beforeVblank;
#endif
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
