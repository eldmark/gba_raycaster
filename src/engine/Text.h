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

// Entero a texto, sin depender de snprintf: en GBA arrastrar printf a la ROM
// por escribir un numero no compensa. Devuelve buf.
const char* intToText(char* buf, int value);
