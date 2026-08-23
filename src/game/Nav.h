#pragma once

#include <cstdint>

#include "Maze.h"

// Campo de flujo hacia el jugador: para cada celda transitable guarda hacia
// donde hay que dar el siguiente paso para acercarse.
//
// Es UNA busqueda en anchura para todos los guardianes, no una por cabeza
// (seccion 20 del PROJECT.md). Persiguiendo en linea recta se quedaban clavados
// contra las paredes de forma permanente: en 60 semillas de prueba, 10 de los
// 25 casos con un guardian despierto acababan con el guardian empujando una
// pared para siempre.
class Nav {
public:
    // Reconstruccion completa y sincrona: hace el BFS entero en la llamada.
    // Solo para los caminos donde la latencia importa mas que el pico de
    // ciclos: cargar un piso y abrir la camara sellada (Game::tryUnlockVault),
    // que ocurren una vez por piso, no dentro del bucle de juego a 60 fps.
    void rebuild(const Maze& maze, int px, int py);

    // Version amortizada para el bucle de juego. beginRebuild() arranca un BFS
    // nuevo en el buffer de fondo sin tocar el que step() esta leyendo; tick()
    // desencola un presupuesto fijo de celdas y, al vaciar la cola, intercambia
    // los dos buffers de un golpe. Llamar a tick() todos los frames es seguro:
    // no hace nada si no hay una reconstruccion en curso.
    void beginRebuild(const Maze& maze, int px, int py);
    void tick(int budget = kDefaultBudget);

    // Paso hacia el jugador desde (x,y). false si esa celda no lleva a ninguna
    // parte: fuera del mapa, dentro de un muro o en una zona incomunicada.
    bool step(int x, int y, int& dx, int& dy) const;

private:
    static constexpr uint8_t NONE = 0xFF;

    // Mismo stride que Maze (docs del port, seccion 1.1): 64x64 celdas caben
    // en un uint16_t empaquetado como x | (y << 8), que es la cola de la fase
    // 1.2. 4096 bytes por buffer, dos buffers, sin malloc.
    static constexpr int kStride = 64;
    static constexpr int kCells = kStride * kStride;
    static constexpr int kDefaultBudget = 40;

    // dir_ es lo que step() lee; dirBack_ es donde se construye el siguiente
    // campo. Los guardianes no ven un campo a medio hacer: solo ven el
    // intercambio ya terminado.
    //
    // Son punteros y no arrays a proposito: intercambiar dos arrays copia los
    // 4096 bytes de cada uno, y eso devuelve al frame de convergencia el pico
    // que esta clase existe para quitar. Con punteros el intercambio son dos
    // registros. El almacenamiento real es fields_, que nadie toca directo.
    uint8_t fields_[2][kCells];
    uint8_t* dir_ = fields_[0];
    uint8_t* dirBack_ = fields_[1];

    // Cola estatica de celdas por visitar, empaquetadas para no pagar la
    // division/modulo de desempaquetar x,y de un indice plano.
    uint16_t queue_[kCells];
    int queueHead_ = 0;
    int queueTail_ = 0;
    bool building_ = false;

    // Vale mientras dura una reconstruccion amortizada: el mapa no cambia de
    // direccion entre beginRebuild() y el tick() que la termina porque ambos
    // los llama Game sobre el mismo Maze miembro.
    const Maze* buildMaze_ = nullptr;

    int width_ = 0;
    int height_ = 0;
};
