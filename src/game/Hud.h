#pragma once

class Framebuffer;
class Game;

// Dibuja el HUD sobre la escena: integridad, archivo, bajas, puntos y la mira.
// Solo tiene sentido mientras se juega.
void drawHud(Framebuffer& fb, const Game& game);

// Dibuja el contador de fotogramas por segundo en la esquina superior
// izquierda. Va aparte de drawHud porque el HUD depende del Game y esto
// depende del bucle principal, que es quien mide el tiempo del frame.
void drawFps(Framebuffer& fb, int fps);

// Dibuja la pantalla de bienvenida, las reglas o el marcador final, segun el
// estado. No hace nada mientras se juega.
void drawScreen(Framebuffer& fb, const Game& game);
