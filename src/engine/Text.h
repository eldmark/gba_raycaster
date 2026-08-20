#pragma once

class Framebuffer;

// Dibuja texto con la fuente 5x7. scale multiplica el tamano del pixel, de modo
// que la misma llamada sirve en los 240x160 de la GBA y en una ventana grande
// de escritorio.
//
// color es un indice de paleta, como todo lo demas del renderizador.
void drawText(Framebuffer& fb, int x, int y, const char* text, unsigned char color,
              int scale = 1);

// Ancho en pixeles que ocuparia el texto. Sirve para centrarlo sin adivinar.
int textWidth(const char* text, int scale = 1);

// Alto de una linea. Quien maqueta no deberia tener que conocer el tamano de
// los glifos.
int textHeight(int scale = 1);

// Igual que drawText pero centrado en la pantalla horizontalmente.
void drawTextCentered(Framebuffer& fb, int y, const char* text, unsigned char color,
                      int scale = 1);

// --- arte ASCII ---------------------------------------------------------------
// Igual que drawText pero SIN separacion entre caracteres y con las lineas
// pegadas. Es lo que hace que los trazos de un dibujo se unan: con el hueco de
// un pixel que lleva el texto normal, una fila de guiones bajos sale punteada y
// una columna de barras sale a trozos.
void drawArtLine(Framebuffer& fb, int x, int y, const char* text,
                 unsigned char color, int scale = 1);

int artWidth(const char* text, int scale = 1);
int artLineHeight(int scale = 1);

// Dibuja varias lineas de arte centradas horizontalmente, desde y hacia abajo.
void drawArtCentered(Framebuffer& fb, int y, const char* const* lines, int count,
                     unsigned char color, int scale = 1);

// Entero a texto, sin depender de snprintf: en GBA arrastrar printf a la ROM
// por escribir un numero no compensa. Devuelve buf.
const char* intToText(char* buf, int value);
