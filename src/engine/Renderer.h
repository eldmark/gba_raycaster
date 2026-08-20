#pragma once

class Framebuffer;
class Maze;
struct Player;

// Vista en primera persona: un rayo por columna de pantalla.
void renderWorld(Framebuffer& fb, const Maze& maze, const Player& player);

// Minimapa 2D en la esquina con el abanico de FOV.
void renderMinimap(Framebuffer& fb, const Maze& maze, const Player& player);
