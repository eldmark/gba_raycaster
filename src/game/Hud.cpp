#include "Hud.h"

#include "Framebuffer.h"
#include "Game.h"
#include "Text.h"
#include "Textures.h"

namespace
{

    // El HUD se dimensiona a partir del ancho de pantalla, tomando los 240 px de la
    // GBA como unidad. Asi el mismo codigo sirve en la consola y en una ventana
    // grande sin dos juegos de constantes.
    int uiScale(const Framebuffer &fb)
    {
        int s = fb.width() / 240;
        return s < 1 ? 1 : s;
    }

    void drawPanel(Framebuffer &fb, int x, int y, int w, int h)
    {
        fb.fillRect(x, y, w, h, PAL_UI_BG);
        // borde de un pixel: separa el panel del mundo sin gastar mas
        fb.fillRect(x, y, w, 1, PAL_UI_DIM);
        fb.fillRect(x, y + h - 1, w, 1, PAL_UI_DIM);
        fb.fillRect(x, y, 1, h, PAL_UI_DIM);
        fb.fillRect(x + w - 1, y, 1, h, PAL_UI_DIM);
    }

    // "MM:SS" a partir de segundos.
    void formatTime(char *buf, int seconds)
    {
        int m = seconds / 60, s = seconds % 60;
        buf[0] = char('0' + (m / 10) % 10);
        buf[1] = char('0' + m % 10);
        buf[2] = ':';
        buf[3] = char('0' + s / 10);
        buf[4] = char('0' + s % 10);
        buf[5] = '\0';
    }

} // namespace

void drawHud(Framebuffer &fb, const Game &game)
{
    if (!game.inWorld())
        return;

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

    // Hitmarker: cuatro trazos diagonales que solo aparecen si el hitscan
    // alcanzo un guardian, no simplemente al apretar el gatillo.
    if (game.hitConfirm())
    {
        const int mark = 5 * s;
        const int dot = (s > 1) ? s : 1;
        for (int p = 0; p < mark; ++p)
        {
            fb.fillRect(cx - gap - p, cy - gap - p, dot, dot, PAL_UI_TEXT);
            fb.fillRect(cx + gap + p, cy - gap - p, dot, dot, PAL_UI_TEXT);
            fb.fillRect(cx - gap - p, cy + gap + p, dot, dot, PAL_UI_TEXT);
            fb.fillRect(cx + gap + p, cy + gap + p, dot, dot, PAL_UI_TEXT);
        }
    }

    // fogonazo: un destello corto desde el borde inferior, que es de donde
    // dispararia el arma si se dibujara
    if (game.muzzleFlash())
    {
        const int fw = 24 * s, fh = 3 * s;
        fb.fillRect(cx - fw / 2, fb.height() - fh, fw, fh, PAL_UI_ACCENT);
    }

    // El pickup explica su efecto al recogerlo, para que RAM, PATCH y CACHE
    // no sean iconos que el jugador tiene que adivinar.
    if (const char *notice = game.pickupNotice())
    {
        drawTextCentered(fb, pad, notice, PAL_UI_ACCENT, s);
    }
    else if (game.protocolNearby())
    {
        drawTextCentered(fb, pad, "PROTOCOLO DETECTADO", PAL_UI_TEXT, s);
    }

    // Llevar la llave encima cambia a donde conviene ir, asi que tiene que
    // verse sin abrir un menu. Desaparece en cuanto la camara queda abierta.
    if (game.hasKey() && !game.vaultOpen())
    {
        drawTextCentered(fb, pad + line, "LLAVE DE ARCHIVO LISTA", PAL_UI_ACCENT, s);
    }

    // --- nucleo centinela -----------------------------------------------------
    // Barra propia arriba del todo: el jefe tiene seis veces la vida de un
    // guardian y sin ella no hay forma de saber si los disparos estan sirviendo.
    if (game.bossAlive())
    {
        const int bw = fb.width() / 2;
        const int bh = 5 * s;
        const int bxx = (fb.width() - bw) / 2;
        const int byy = pad + line * 2;
        drawTextCentered(fb, byy - textHeight(s) - s, "NUCLEO CENTINELA",
                         PAL_UI_ACCENT, s);
        fb.fillRect(bxx, byy, bw, bh, PAL_UI_DIM);
        const int hpMax = game.bossHpMax();
        const int fillW = hpMax > 0 ? bw * game.bossHp() / hpMax : 0;
        if (fillW > 0)
            fb.fillRect(bxx, byy, fillW, bh, PAL_UI_WARN);
    }

    // --- integridad -----------------------------------------------------------
    // El panel se mide a partir del texto, no al reves: fijarlo al ancho de la
    // barra hacia que la etiqueta y el numero se solaparan.
    intToText(buf, game.hp());
    const int labelW = textWidth("INTEGRIDAD", s);
    const int valueW = textWidth("000", s); // el ancho maximo, no el de ahora
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
    if (fill > 0)
    {
        fb.fillRect(bx, by, fill, barH, low ? PAL_UI_WARN : PAL_UI_ACCENT);
    }

    // --- archivo, bajas, buffs y puntos ---------------------------------------
    // Se arman las filas primero para poder medir el panel: sin fondo, el
    // texto se pierde sobre una pared clara.
    char rows[4][24];
    int i = 0;
    for (const char *t = "ARCHIVO "; *t; ++t)
        rows[0][i++] = *t;
    intToText(rows[0] + i, game.floor());
    while (rows[0][i])
        ++i;
    rows[0][i++] = '/';
    intToText(rows[0] + i, Game::FINAL_FLOOR);

    i = 0;
    for (const char *t = "BAJAS "; *t; ++t)
        rows[1][i++] = *t;
    intToText(rows[1] + i, game.kills());

    i = 0;
    for (const char *t = "BUFFS "; *t; ++t)
        rows[2][i++] = *t;
    intToText(rows[2] + i, game.buffCount());

    i = 0;
    for (const char *t = "PUNTOS "; *t; ++t)
        rows[3][i++] = *t;
    intToText(rows[3] + i, game.score());

    int statW = 0;
    for (const auto &r : rows)
    {
        int w = textWidth(r, s);
        if (w > statW)
            statW = w;
    }

    // Va abajo a la derecha y no arriba: arriba es donde salen el aviso de
    // recogida, la llave y la barra del nucleo, y un panel fijo en esa esquina
    // se comia justo la parte del pasillo que se mira al avanzar. Abajo solo
    // comparte borde con el panel de integridad, que esta en la otra esquina.
    //
    // Y con su propio padding, mas apretado que el del panel de integridad:
    // en los 240x160 de la consola la fuente ya esta a escala 1 y no se puede
    // encoger, asi que lo que se recorta es el aire alrededor del texto.
    const int statPad = 2 * s;
    const int statLine = textHeight(s) + s;
    const int statPanelW = statW + statPad * 2;
    const int statPanelH = statLine * 4 + statPad * 2 - s;
    const int sx = fb.width() - statPanelW - pad;
    const int sy = fb.height() - statPanelH - pad;
    drawPanel(fb, sx, sy, statPanelW, statPanelH);

    const unsigned char statColor[4] = {PAL_UI_TEXT, PAL_UI_DIM, PAL_UI_TEXT, PAL_UI_ACCENT};
    int ry = sy + statPad;
    for (int r = 0; r < 4; ++r)
    {
        drawText(fb, sx + statPanelW - statPad - textWidth(rows[r], s), ry, rows[r],
                 statColor[r], s);
        ry += statLine;
    }

    // --- pausa ----------------------------------------------------------------
    // No se limpia la pantalla como hacen las demas pantallas: en pausa
    // interesa seguir viendo donde quedo uno. Solo una banda con el aviso.
    if (game.paused())
    {
        const int bh = textHeight(s * 2) + pad * 2;
        const int by = (fb.height() - bh) / 2;
        fb.fillRect(0, by, fb.width(), bh, PAL_UI_BG);
        fb.fillRect(0, by, fb.width(), s, PAL_UI_ACCENT);
        fb.fillRect(0, by + bh - s, fb.width(), s, PAL_UI_ACCENT);
        drawTextCentered(fb, by + pad, "PAUSA", PAL_UI_ACCENT, s * 2);
        drawTextCentered(fb, by + bh + pad, "START CONTINUA", PAL_UI_TEXT, s);
    }
}

void drawFps(Framebuffer &fb, int fps)
{
    const int s = uiScale(fb);
    char buf[12];
    int i = 0;
    for (const char *t = "FPS "; *t; ++t)
        buf[i++] = *t;
    intToText(buf + i, fps);
    // Arriba a la izquierda, sin panel: el contador es instrumental, no parte
    // de la ficcion del HUD, y un fondo mas seria un rectangulo mas que tapa
    // mundo. El color de acento se lee sobre cualquier pared.
    drawText(fb, 2 * s, 2 * s, buf, PAL_UI_ACCENT, s);
}

namespace
{

    // Logotipo. Arte ASCII a proposito y no un mapa de bits: el juego va de estar
    // dentro de una maquina, y un dibujo hecho de caracteres dice eso solo.
    //
    // Se dibuja sin separacion entre caracteres (drawArtCentered), o los guiones
    // bajos saldrian punteados y las barras verticales a trozos.
    const char *const HAT[] = {
        "     _______     ",
        "    /       \\    ",
        "   |         |   ",
        "   |         |   ",
        "   |=========|   ",
        "   |         |   ",
        " __|_________|__ ",
        "|               |",
        "|_______________|",
    };
    constexpr int HAT_LINES = int(sizeof(HAT) / sizeof(HAT[0]));

    // Una linea de una pantalla. El texto vacio deja un hueco.
    struct Row
    {
        const char *text;
        unsigned char color;
        int scale;
    };

    // Dibuja las filas como un bloque centrado verticalmente. Se mide primero y se
    // dibuja despues: maquetar hacia abajo sin medir es como el titulo acababa
    // fuera de la pantalla.
    void drawBlock(Framebuffer &fb, const Row *rows, int count, int gap, int topLimit,
                   int bottomLimit)
    {
        auto measure = [&](int g)
        {
            int t = 0;
            for (int i = 0; i < count; ++i)
                t += textHeight(rows[i].scale) + g;
            return t > 0 ? t - g : 0;
        };

        // Si el bloque no cabe se cierra la separacion entre lineas antes de
        // dejarlo desbordar. Sin esto una linea de mas en las reglas empujaba
        // el texto por encima de la banda superior y por debajo del aviso de
        // START, y en 240x160 eso no se ve hasta que ya esta en pantalla.
        const int room = bottomLimit - topLimit;
        while (gap > 0 && measure(gap) > room)
            --gap;
        const int total = measure(gap);

        int y = topLimit + (room - total) / 2;
        if (y < topLimit)
            y = topLimit;

        for (int i = 0; i < count; ++i)
        {
            if (rows[i].text[0])
            {
                drawTextCentered(fb, y, rows[i].text, rows[i].color, rows[i].scale);
            }
            y += textHeight(rows[i].scale) + gap;
        }
    }

} // namespace

void drawScreen(Framebuffer &fb, const Game &game)
{
    const Game::State st = game.state();
    if (game.inWorld())
        return;

    const int s = uiScale(fb);
    const int gap = 3 * s;
    char buf[24];
    char rowText[7][32];

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

    if (st == Game::State::Title)
    {
        // El bloque se mide entero antes de dibujar nada, logotipo incluido:
        // maquetar hacia abajo dejaba el aviso de START fuera de la pantalla.
        const int hatH = artLineHeight(s) * HAT_LINES;
        const int titleH = textHeight(s * 2);
        const int subH = textHeight(s);
        const int total = hatH + gap * 2 + titleH + gap + subH + gap * 3 + subH;

        int y = topLimit + (bottomLimit - topLimit - total) / 2;
        if (y < topLimit)
            y = topLimit;

        drawArtCentered(fb, y, HAT, HAT_LINES, PAL_UI_ACCENT, s);
        y += hatH + gap * 2;
        drawTextCentered(fb, y, "VIOLET HAT", PAL_UI_ACCENT, s * 2);
        y += titleH + gap;
        drawTextCentered(fb, y, "PROTOCOLO DE INFILTRACION", PAL_UI_TEXT, s);
        y += subH + gap * 3;

        // Selector de archivo. Las flechas se dibujan solo del lado al que
        // todavia se puede mover, que es lo que informa de donde estan los
        // topes sin escribir "1 de 5" en ninguna parte.
        int i = 0;
        rowText[0][i++] = game.startFloor() > 1 ? '<' : ' ';
        rowText[0][i++] = ' ';
        for (const char *t = "ARCHIVO "; *t; ++t)
            rowText[0][i++] = *t;
        intToText(rowText[0] + i, game.startFloor());
        while (rowText[0][i])
            ++i;
        rowText[0][i++] = ' ';
        rowText[0][i++] = game.startFloor() < Game::FINAL_FLOOR ? '>' : ' ';
        rowText[0][i] = '\0';
        drawTextCentered(fb, y, rowText[0], PAL_UI_ACCENT, s);

        drawTextCentered(fb, promptY, "FLECHAS ELIGEN - START CONTINUA", PAL_UI_TEXT, s);
        return;
    }

    if (st == Game::State::Rules)
    {
        // Doce lineas es lo que cabe en los 240x160 de la consola contando el
        // aviso de START. Cada regla nueva sustituye a una vieja, no se suma.
        const Row rows[] = {
            {"DENTRO DE UN SISTEMA QUE LLEVA", PAL_UI_DIM, s},
            {"MUCHO TIEMPO SIN VISITAS", PAL_UI_DIM, s},
            {"", PAL_UI_TEXT, s},
            {"REGLAS", PAL_UI_ACCENT, s * 2},
            {"", PAL_UI_TEXT, s},
            {"LOS GUARDIANES DESPIERTAN AL VERTE", PAL_UI_TEXT, s},
            {"DISPARA CON A: NO CRUZA MUROS", PAL_UI_TEXT, s},
            {"LA SALIDA REPARA Y TE LLEVA AL", PAL_UI_TEXT, s},
            {"SIGUIENTE ARCHIVO", PAL_UI_TEXT, s},
            {"LA LLAVE ABRE LA CAMARA SELLADA", PAL_UI_TEXT, s},
            {"", PAL_UI_TEXT, s},
            {"EL NUCLEO CENTINELA GUARDA", PAL_UI_ACCENT, s},
            {"LA ULTIMA SALIDA", PAL_UI_ACCENT, s},
        };
        drawBlock(fb, rows, int(sizeof(rows) / sizeof(rows[0])), gap, topLimit,
                  bottomLimit);
        drawTextCentered(fb, promptY, "START PARA CONECTAR", PAL_UI_ACCENT, s);
        return;
    }

    if (st == Game::State::Transition)
    {
        const Row rows[] = {
            {"RUTA ASEGURADA", PAL_UI_ACCENT, s * 2},
            {"", PAL_UI_TEXT, s},
            {"PROTOCOLO ENLAZADO", PAL_UI_TEXT, s},
            {"PASANDO A SIGUIENTE FASE", PAL_UI_TEXT, s},
            {"", PAL_UI_TEXT, s},
            {"NUEVO ARCHIVO CARGADO", PAL_UI_DIM, s},
        };
        drawBlock(fb, rows, int(sizeof(rows) / sizeof(rows[0])), gap, topLimit,
                  fb.height() - 4 * s);
        return;
    }

    // --- marcador final (seccion 28 del PROJECT.md) ---------------------------
    const bool won = st == Game::State::Cleared;

    struct Stat
    {
        const char *label;
        int value;
    };
    const Stat stats[] = {
        {"ARCHIVO", game.floor()},
        {"BAJAS", game.kills()},
        {"BUFFS", game.buffCount()},
        {"PUNTOS", game.score()},
    };
    for (int r = 0; r < 4; ++r)
    {
        int i = 0;
        for (const char *t = stats[r].label; *t; ++t)
            rowText[r][i++] = *t;
        rowText[r][i++] = ':';
        rowText[r][i++] = ' ';
        intToText(rowText[r] + i, stats[r].value);
    }

    formatTime(buf, game.elapsedSeconds());
    int i = 0;
    for (const char *t = "TIEMPO: "; *t; ++t)
        rowText[4][i++] = *t;
    for (const char *t = buf; *t; ++t)
        rowText[4][i++] = *t;
    rowText[4][i] = '\0';

    // La semilla se muestra para poder repetir exactamente esta run.
    i = 0;
    for (const char *t = "RAM/PATCH/CACHE: "; *t; ++t)
        rowText[5][i++] = *t;
    intToText(rowText[5] + i, game.ramBuffs());
    while (rowText[5][i]) ++i;
    rowText[5][i++] = '/';
    intToText(rowText[5] + i, game.patchBuffs());
    while (rowText[5][i]) ++i;
    rowText[5][i++] = '/';
    intToText(rowText[5] + i, game.cacheBuffs());

    i = 0;
    for (const char *t = "SEED: "; *t; ++t)
        rowText[6][i++] = *t;
    intToText(rowText[6] + i, int(game.seed()));

    const Row rows[] = {
        {won ? "EXTRACCION COMPLETA" : "CONEXION PERDIDA",
         (unsigned char)(won ? PAL_UI_ACCENT : PAL_UI_WARN), s * 2},
        {"", PAL_UI_TEXT, s},
        {rowText[0], PAL_UI_TEXT, s},
        {rowText[1], PAL_UI_TEXT, s},
        {rowText[2], PAL_UI_TEXT, s},
        {rowText[3], PAL_UI_ACCENT, s},
        {rowText[4], PAL_UI_TEXT, s},
        {rowText[5], PAL_UI_DIM, s},
        {"", PAL_UI_TEXT, s},
        {rowText[6], PAL_UI_DIM, s},
    };
    drawBlock(fb, rows, int(sizeof(rows) / sizeof(rows[0])), gap, topLimit,
              bottomLimit);
    drawTextCentered(fb, promptY, "START PARA VOLVER", PAL_UI_ACCENT, s);
}
