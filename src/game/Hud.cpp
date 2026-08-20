#include "Hud.h"

#include "Framebuffer.h"
#include "Game.h"
#include "Text.h"
#include "Textures.h"

namespace {

// El HUD se dimensiona a partir del ancho de pantalla, tomando los 240 px de la
// GBA como unidad. Asi el mismo codigo sirve en la consola y en una ventana
// grande sin dos juegos de constantes.
int uiScale(const Framebuffer& fb) {
    int s = fb.width() / 240;
    return s < 1 ? 1 : s;
}

void drawPanel(Framebuffer& fb, int x, int y, int w, int h) {
    fb.fillRect(x, y, w, h, PAL_UI_BG);
    // borde de un pixel: separa el panel del mundo sin gastar mas
    fb.fillRect(x, y, w, 1, PAL_UI_DIM);
    fb.fillRect(x, y + h - 1, w, 1, PAL_UI_DIM);
    fb.fillRect(x, y, 1, h, PAL_UI_DIM);
    fb.fillRect(x + w - 1, y, 1, h, PAL_UI_DIM);
}

// "MM:SS" a partir de segundos.
void formatTime(char* buf, int seconds) {
    int m = seconds / 60, s = seconds % 60;
    buf[0] = char('0' + (m / 10) % 10);
    buf[1] = char('0' + m % 10);
    buf[2] = ':';
    buf[3] = char('0' + s / 10);
    buf[4] = char('0' + s % 10);
    buf[5] = '\0';
}

}  // namespace

void drawHud(Framebuffer& fb, const Game& game) {
    if (game.state() != Game::State::Playing) return;

    const int s = uiScale(fb);
    const int pad = 4 * s;
    const int line = textHeight(s) + 3 * s;
    char buf[16];

    // --- mira -----------------------------------------------------------------
    // Cuatro trazos y un hueco en medio: una cruz llena taparia justo lo que se
    // esta apuntando.
    const int cx = fb.width() / 2, cy = fb.height() / 2;
    const int arm = 4 * s, gap = 2 * s, th = (s > 1) ? s : 1;
    const unsigned char aim = game.muzzleFlash() ? PAL_UI_ACCENT : PAL_UI_TEXT;
    fb.fillRect(cx - gap - arm, cy, arm, th, aim);
    fb.fillRect(cx + gap, cy, arm, th, aim);
    fb.fillRect(cx, cy - gap - arm, th, arm, aim);
    fb.fillRect(cx, cy + gap, th, arm, aim);

    // fogonazo: un destello corto desde el borde inferior, que es de donde
    // dispararia el arma si se dibujara
    if (game.muzzleFlash()) {
        const int fw = 24 * s, fh = 3 * s;
        fb.fillRect(cx - fw / 2, fb.height() - fh, fw, fh, PAL_UI_ACCENT);
    }

    // --- integridad -----------------------------------------------------------
    // El panel se mide a partir del texto, no al reves: fijarlo al ancho de la
    // barra hacia que la etiqueta y el numero se solaparan.
    intToText(buf, game.hp());
    const int labelW = textWidth("INTEGRIDAD", s);
    const int valueW = textWidth("000", s);  // el ancho maximo, no el de ahora
    const int textRow = labelW + 3 * s + valueW;
    const int barW = (textRow > 72 * s) ? textRow : 72 * s;
    const int barH = 6 * s;
    const int panelW = barW + pad * 2;
    const int panelH = barH + line + pad * 2;
    const int px = pad, py = fb.height() - panelH - pad;
    drawPanel(fb, px, py, panelW, panelH);

    drawText(fb, px + pad, py + pad, "INTEGRIDAD", PAL_UI_TEXT, s);
    drawText(fb, px + panelW - pad - textWidth(buf, s), py + pad, buf,
             (game.hp() * 4 <= game.hpMax()) ? PAL_UI_ACCENT : PAL_UI_TEXT, s);

    const int bx = px + pad, by = py + pad + line;
    fb.fillRect(bx, by, barW, barH, PAL_UI_DIM);
    // por debajo de un cuarto la barra cambia de color: el numero solo no se
    // mira cuando hay un guardian encima
    const bool low = game.hp() * 4 <= game.hpMax();
    int fill = game.hpMax() > 0 ? barW * game.hp() / game.hpMax() : 0;
    if (fill > 0) {
        fb.fillRect(bx, by, fill, barH, low ? PAL_UI_WARN : PAL_UI_ACCENT);
    }

    // --- archivo, bajas y puntos ----------------------------------------------
    // Se arman las tres filas primero para poder medir el panel: sin fondo, el
    // texto se pierde sobre una pared clara.
    char rows[3][24];
    int i = 0;
    for (const char* t = "ARCHIVO "; *t; ++t) rows[0][i++] = *t;
    intToText(rows[0] + i, game.floor());
    while (rows[0][i]) ++i;
    rows[0][i++] = '/';
    intToText(rows[0] + i, Game::FINAL_FLOOR);

    i = 0;
    for (const char* t = "BAJAS "; *t; ++t) rows[1][i++] = *t;
    intToText(rows[1] + i, game.kills());

    i = 0;
    for (const char* t = "PUNTOS "; *t; ++t) rows[2][i++] = *t;
    intToText(rows[2] + i, game.score());

    int statW = 0;
    for (const auto& r : rows) {
        int w = textWidth(r, s);
        if (w > statW) statW = w;
    }

    const int statPanelW = statW + pad * 2;
    const int statPanelH = line * 3 + pad * 2 - 3 * s;
    const int sx = fb.width() - statPanelW - pad;
    drawPanel(fb, sx, pad, statPanelW, statPanelH);

    const unsigned char statColor[3] = {PAL_UI_TEXT, PAL_UI_DIM, PAL_UI_ACCENT};
    int ry = pad + pad;
    for (int r = 0; r < 3; ++r) {
        drawText(fb, sx + statPanelW - pad - textWidth(rows[r], s), ry, rows[r],
                 statColor[r], s);
        ry += line;
    }
}

namespace {

// Una linea de una pantalla. El texto vacio deja un hueco.
struct Row {
    const char* text;
    unsigned char color;
    int scale;
};

// Dibuja las filas como un bloque centrado verticalmente. Se mide primero y se
// dibuja despues: maquetar hacia abajo sin medir es como el titulo acababa
// fuera de la pantalla.
void drawBlock(Framebuffer& fb, const Row* rows, int count, int gap, int topLimit,
               int bottomLimit) {
    int total = 0;
    for (int i = 0; i < count; ++i) total += textHeight(rows[i].scale) + gap;
    if (total > 0) total -= gap;

    int y = topLimit + (bottomLimit - topLimit - total) / 2;
    if (y < topLimit) y = topLimit;

    for (int i = 0; i < count; ++i) {
        if (rows[i].text[0]) {
            drawTextCentered(fb, y, rows[i].text, rows[i].color, rows[i].scale);
        }
        y += textHeight(rows[i].scale) + gap;
    }
}

}  // namespace

void drawScreen(Framebuffer& fb, const Game& game) {
    const Game::State st = game.state();
    if (st == Game::State::Playing) return;

    const int s = uiScale(fb);
    const int gap = 3 * s;
    char buf[24];
    char rowText[6][32];

    fb.clear(PAL_UI_BG);

    // Una banda de acento arriba y otra abajo: enmarca la pantalla sin dibujar
    // un marco entero.
    fb.fillRect(0, 0, fb.width(), s, PAL_UI_ACCENT);
    fb.fillRect(0, fb.height() - s, fb.width(), s, PAL_UI_ACCENT);

    // El aviso de START va anclado abajo, no al final del flujo: asi no se sale
    // de la pantalla por mucho que crezca el texto de encima.
    const int promptY = fb.height() - textHeight(s) - 6 * s;
    const int bottomLimit = promptY - 4 * s;
    const int topLimit = 4 * s;

    if (st == Game::State::Title) {
        const Row rows[] = {
            {"ARCHIVO", PAL_UI_ACCENT, s * 4},
            {"", PAL_UI_TEXT, s},
            {"PROTOCOLO DE INFILTRACION", PAL_UI_TEXT, s},
            {"", PAL_UI_TEXT, s},
            {"ERES UN PROCESO SIN FIRMA", PAL_UI_DIM, s},
            {"DENTRO DE UN SISTEMA QUE LLEVA", PAL_UI_DIM, s},
            {"MUCHO TIEMPO SIN VISITAS", PAL_UI_DIM, s},
        };
        drawBlock(fb, rows, int(sizeof(rows) / sizeof(rows[0])), gap, topLimit,
                  bottomLimit);
        drawTextCentered(fb, promptY, "START PARA CONTINUAR", PAL_UI_TEXT, s);
        return;
    }

    if (st == Game::State::Rules) {
        const Row rows[] = {
            {"REGLAS", PAL_UI_ACCENT, s * 2},
            {"", PAL_UI_TEXT, s},
            {"LOS GUARDIANES DUERMEN.", PAL_UI_TEXT, s},
            {"DESPIERTAN CUANDO TE ACERCAS.", PAL_UI_TEXT, s},
            {"", PAL_UI_TEXT, s},
            {"DISPARA CON A.", PAL_UI_TEXT, s},
            {"EL DISPARO NO ATRAVIESA MUROS.", PAL_UI_TEXT, s},
            {"", PAL_UI_TEXT, s},
            {"LA SALIDA ENLAZA AL SIGUIENTE", PAL_UI_TEXT, s},
            {"ARCHIVO Y REPARA TU INTEGRIDAD.", PAL_UI_TEXT, s},
            {"", PAL_UI_TEXT, s},
            {"CINCO ARCHIVOS Y ESTAS FUERA.", PAL_UI_ACCENT, s},
        };
        drawBlock(fb, rows, int(sizeof(rows) / sizeof(rows[0])), gap, topLimit,
                  bottomLimit);
        drawTextCentered(fb, promptY, "START PARA CONECTAR", PAL_UI_ACCENT, s);
        return;
    }

    // --- marcador final (seccion 28 del PROJECT.md) ---------------------------
    const bool won = st == Game::State::Cleared;

    struct Stat { const char* label; int value; };
    const Stat stats[] = {
        {"ARCHIVO", game.floor()},
        {"BAJAS", game.kills()},
        {"PUNTOS", game.score()},
    };
    for (int r = 0; r < 3; ++r) {
        int i = 0;
        for (const char* t = stats[r].label; *t; ++t) rowText[r][i++] = *t;
        rowText[r][i++] = ':';
        rowText[r][i++] = ' ';
        intToText(rowText[r] + i, stats[r].value);
    }

    formatTime(buf, game.elapsedSeconds());
    int i = 0;
    for (const char* t = "TIEMPO: "; *t; ++t) rowText[3][i++] = *t;
    for (const char* t = buf; *t; ++t) rowText[3][i++] = *t;
    rowText[3][i] = '\0';

    // La semilla se muestra para poder repetir exactamente esta run.
    i = 0;
    for (const char* t = "SEED: "; *t; ++t) rowText[4][i++] = *t;
    intToText(rowText[4] + i, int(game.seed()));

    const Row rows[] = {
        {won ? "EXTRACCION COMPLETA" : "CONEXION PERDIDA",
         (unsigned char)(won ? PAL_UI_ACCENT : PAL_UI_WARN), s * 2},
        {"", PAL_UI_TEXT, s},
        {rowText[0], PAL_UI_TEXT, s},
        {rowText[1], PAL_UI_TEXT, s},
        {rowText[2], PAL_UI_ACCENT, s},
        {rowText[3], PAL_UI_TEXT, s},
        {"", PAL_UI_TEXT, s},
        {rowText[4], PAL_UI_DIM, s},
    };
    drawBlock(fb, rows, int(sizeof(rows) / sizeof(rows[0])), gap, topLimit,
              bottomLimit);
    drawTextCentered(fb, promptY, "START PARA VOLVER", PAL_UI_ACCENT, s);
}
