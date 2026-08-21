#pragma once

// Mezclador minimo sobre el SDL2 que ya usa la ventana. No se usa SDL2_mixer a
// proposito: los siete WAV del juego son mono de 16 kHz y 16 bits, exactamente
// el formato con el que se abre el dispositivo, asi que no hay conversion que
// hacer y una dependencia mas no compraria nada.
//
// Vive en la capa de escritorio, como Platform: en GBA el sonido sale por los
// canales de DMA del hardware y este archivo no se porta.
class Audio {
public:
    // Efectos de una sola vez.
    enum Sfx { Shot, Damage, Pickup, LevelUp, Victory, SFX_COUNT };
    // Temas de fondo, en bucle.
    enum Track { None, Menu, Assault, TRACK_COUNT };

    // dir es la carpeta que contiene los .wav. Si falta un archivo el juego
    // sigue funcionando en silencio: el audio no es motivo para no arrancar.
    bool init(const char* dir);
    void shutdown();

    void play(Sfx sfx);
    // Cambiar al tema que ya suena no lo reinicia.
    void setTrack(Track track);

private:
    bool ready_ = false;
};
