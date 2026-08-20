#pragma once

#include <cstdint>
#include <vector>

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
    // Recalcula el campo desde la celda del jugador. Cuesta un recorrido del
    // mapa entero, por eso quien llama lo hace cada varios frames y no cada uno.
    void rebuild(const Maze& maze, int px, int py);

    // Paso hacia el jugador desde (x,y). false si esa celda no lleva a ninguna
    // parte: fuera del mapa, dentro de un muro o en una zona incomunicada.
    bool step(int x, int y, int& dx, int& dy) const;

private:
    static constexpr uint8_t NONE = 0xFF;

    // ponytail: un byte por celda; 48x48 son 2304 bytes, que en la EWRAM de la
    // GBA no es nada. Si el mapa creciera, esto es lo primero que hay que mirar.
    std::vector<uint8_t> dir_;
    int width_ = 0;
    int height_ = 0;
};
