#pragma once

#include "Player.h"  // Input

class Framebuffer;

// Capa de plataforma: ventana, entrada y volcado a pantalla.
// Este archivo entero se reemplaza en el port a GBA.
class Platform {
public:
    bool init(int w, int h, const char* title);

    // Llena `input` con el estado del teclado.
    // Devuelve false cuando el usuario cierra la ventana o pulsa ESC.
    bool pollInput(Input& input);

    void present(const Framebuffer& fb);
    void setTitle(const char* title);
    void shutdown();

    // Reloj monotonico en milisegundos. Vive en Platform para que el bucle
    // principal mida dt sin tocar SDL (en GBA sera el contador de VBlank).
    unsigned long long ticksMs() const;

private:
    void* window_ = nullptr;
    void* renderer_ = nullptr;
    void* texture_ = nullptr;
    bool sdlReady_ = false;
    int width_ = 0, height_ = 0;
};
