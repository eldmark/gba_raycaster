#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

// Reproductor de referencia para DirectSound de GBA. Ver tools/audio_gba_player.md
// para el diseno completo (por que timer 0, por que FIFO A/B, presupuesto de
// EWRAM). Autonomo a proposito: no incluye nada de src/engine, src/game ni
// src/desktop, para no depender del esqueleto GBA que todavia no existe en
// este worktree.

#include <stdint.h>

// Musica: se guarda su tasa nativa porque se hornea mas comprimida (10512 Hz)
// que la tasa de reproduccion de hardware (16384 Hz) para ahorrar ROM; el
// mezclador la remuestrea con un acumulador de fase.
typedef struct {
    const int8_t* data;
    uint32_t len;   // muestras, no bytes (PCM8 = lo mismo, pero por claridad)
    uint32_t rate;  // Hz nativo del asset horneado
} AudioTrack;

// Efectos: horneados ya a la tasa de reproduccion de hardware (16384 Hz), se
// copian a la FIFO de efectos sin remuestrear.
typedef struct {
    const int8_t* data;
    uint32_t len;
} AudioClip;

// Arranca SOUNDCNT, timer 0 y los dos canales de DMA en modo FIFO. Se llama
// una vez al inicio del juego.
void gbaAudioInit(void);

// Cambia la pista de fondo. NULL detiene la musica sin detener los efectos.
// Igual que el mezclador de escritorio: si ya es la pista que suena, no la
// reinicia.
void gbaAudioSetMusic(const AudioTrack* track);

// Dispara un efecto de una sola vez. Se pierde si las 4 voces ya estan
// ocupadas, igual que el mezclador de escritorio (src/desktop/Audio.cpp):
// mejor perder un efecto que cortar uno que ya suena.
void gbaAudioPlaySfx(const AudioClip* clip);

// Llamar una vez por VBlank (desde el ISR de VBlank del Platform de GBA,
// cuando exista). Rellena la mitad de cada buffer que el DMA ya termino de
// reproducir con muestras nuevas: musica remuestreada mas la mezcla de las
// voces de efectos activas.
void gbaAudioVBlank(void);

#endif  // AUDIO_PLAYER_H
