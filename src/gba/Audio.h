#pragma once

// Audio de GBA por DirectSound. Misma interfaz que src/desktop/Audio.h -a
// proposito- para que el bucle principal de cada plataforma haga las mismas
// llamadas: la capa de juego sigue sin saber que existe el sonido.
//
// El diseno de fondo (por que DirectSound y no los 4 canales DMG, por que
// FIFO A para musica y FIFO B para efectos) esta en tools/audio_gba_player.md.
// Lo que cambio al aterrizarlo aqui es la tasa de reproduccion: 10512 Hz para
// todo, ver el comentario del .cpp.
class Audio {
public:
    // Los mismos identificadores que la version de escritorio.
    enum Sfx { Shot, Damage, Pickup, LevelUp, Victory, SFX_COUNT };
    enum Track { None, Menu, Assault, TRACK_COUNT };

    // Arranca el hardware de sonido y engancha el mezclador al IRQ de VBlank.
    // Devuelve siempre true: no hay nada que pueda fallar en un cartucho.
    bool init();
    void shutdown();

    void play(Sfx sfx);
    // Cambiar al tema que ya suena no lo reinicia.
    void setTrack(Track track);
};
