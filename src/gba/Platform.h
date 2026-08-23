#pragma once

#include "Player.h"  // Input

class Framebuffer;

// Capa de plataforma para GBA: KEYPAD en vez de SDL, VRAM en vez de ventana.
// Misma interfaz que src/desktop/Platform.h para que main.cpp y el resto del
// juego no sepan sobre que hardware corren.
class Platform {
public:
    bool init(int w, int h, const char* title);

    // Llena `input` con el estado del KEYPAD. Nunca devuelve false: en la GBA
    // no hay "cerrar la ventana", el juego corre hasta que se apaga la
    // consola.
    bool pollInput(Input& input);

    void present(const Framebuffer& fb);
    void setTitle(const char* title);
    void shutdown();

    // Contador de VBlanks transcurridos, no milisegundos: la GBA no tiene
    // reloj de pared. A 59,7275 Hz un VBlank son ~16,74 ms, suficiente para
    // que Player.cpp integre con un dt razonable.
    unsigned long long ticksMs() const;

private:
    int width_ = 0, height_ = 0;
};
