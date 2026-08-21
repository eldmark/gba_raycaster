#include "audio_player.h"

#include <stddef.h>

// Registros de hardware escritos a mano (no se incluye gba_sound.h de
// libgba): este archivo se queda autonomo, sin depender de que el esqueleto
// GBA (fase 3, todavia no existe en este worktree) haya fijado sus rutas de
// include o su toolchain file.
#define REG_BASE 0x04000000u

#define REG_SOUNDCNT_L (*(volatile uint16_t*)(REG_BASE + 0x080))
#define REG_SOUNDCNT_H (*(volatile uint16_t*)(REG_BASE + 0x082))
#define REG_SOUNDCNT_X (*(volatile uint16_t*)(REG_BASE + 0x084))
#define REG_FIFO_A (*(volatile uint32_t*)(REG_BASE + 0x0A0))
#define REG_FIFO_B (*(volatile uint32_t*)(REG_BASE + 0x0A4))

#define REG_TM0CNT_L (*(volatile uint16_t*)(REG_BASE + 0x100))
#define REG_TM0CNT_H (*(volatile uint16_t*)(REG_BASE + 0x102))

#define REG_DMA1SAD (*(volatile const void**)(REG_BASE + 0x0BC))
#define REG_DMA1DAD (*(volatile void**)(REG_BASE + 0x0C0))
#define REG_DMA1CNT_H (*(volatile uint16_t*)(REG_BASE + 0x0C6))

#define REG_DMA2SAD (*(volatile const void**)(REG_BASE + 0x0C8))
#define REG_DMA2DAD (*(volatile void**)(REG_BASE + 0x0CC))
#define REG_DMA2CNT_H (*(volatile uint16_t*)(REG_BASE + 0x0CE))

// SOUNDCNT_H: DirectSound A/B a volumen 100%, ambas timer 0, resetear FIFO.
#define SOUNDCNT_H_INIT \
    ((1u << 2) | (1u << 3) |                  /* DSound A/B volumen 100% */ \
     (1u << 8) | (1u << 9) |                  /* DSound A a ambos oidos */ \
     (0u << 10) |                             /* DSound A usa timer 0 */ \
     (1u << 11) |                             /* reset FIFO A */ \
     (1u << 12) | (1u << 13) |                /* DSound B a ambos oidos */ \
     (0u << 14) |                             /* DSound B usa timer 0 */ \
     (1u << 15))                              /* reset FIFO B */

// DMA en modo "Special/Sound timing": fija destino, fuente incremental,
// tamano de 32 bits, repetir. El disparo real lo hace la senal de FIFO vacia
// del hardware de sonido, no el conteo del propio DMA.
#define DMA_DST_FIXED (2u << 5)
#define DMA_SRC_INC (0u << 7)
#define DMA_REPEAT (1u << 9)
#define DMA_32BIT (1u << 10)
#define DMA_TIMING_SPECIAL (3u << 12)
#define DMA_ENABLE (1u << 15)
#define DMA_SOUND_CTRL \
    (DMA_DST_FIXED | DMA_SRC_INC | DMA_REPEAT | DMA_32BIT | DMA_TIMING_SPECIAL | DMA_ENABLE)

#define HW_RATE 16384u
#define HALF_LEN 320u  // multiplo de 4, holgura sobre ~274 muestras/vblank a 16384 Hz
#define BUF_LEN (HALF_LEN * 2u)
#define MAX_VOICES 4  // mismo limite que src/desktop/Audio.cpp

static int8_t s_musicBuf[BUF_LEN] __attribute__((aligned(4)));
static int8_t s_sfxBuf[BUF_LEN] __attribute__((aligned(4)));

// half=0/1 indica cual mitad se debe rellenar a continuacion (la que el DMA
// ya termino de consumir). Arranca en 1: la mitad 0 se llena en init y se
// reproduce primero.
static uint8_t s_nextHalf = 1;

typedef struct {
    const AudioTrack* track;
    uint32_t phase;  // acumulador Q8.8: entero = indice de muestra nativa
    uint32_t step;   // (track->rate << 8) / HW_RATE
} MusicState;

typedef struct {
    const AudioClip* clip;
    uint32_t pos;
} Voice;

static MusicState s_music;
static Voice s_voices[MAX_VOICES];

static inline int8_t clampS8(int v) {
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}

// Escribe HALF_LEN muestras de musica remuestreada (vecino mas cercano,
// acumulador de fase) en dst. Si no hay pista activa, silencio.
static void fillMusic(int8_t* dst) {
    if (!s_music.track || !s_music.track->data || s_music.track->len == 0) {
        for (uint32_t i = 0; i < HALF_LEN; i++) dst[i] = 0;
        return;
    }
    const AudioTrack* t = s_music.track;
    for (uint32_t i = 0; i < HALF_LEN; i++) {
        uint32_t idx = s_music.phase >> 8;
        if (idx >= t->len) {
            idx = 0;
            s_music.phase -= (uint32_t)t->len << 8;  // loop sin discontinuidad de fase
        }
        dst[i] = t->data[idx];
        s_music.phase += s_music.step;
    }
}

// Suma hasta MAX_VOICES efectos activos en dst, ya a la tasa de hardware
// (los efectos se hornean a HW_RATE, sin remuestrear).
static void fillSfx(int8_t* dst) {
    for (uint32_t i = 0; i < HALF_LEN; i++) {
        int acc = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice* voice = &s_voices[v];
            if (!voice->clip) continue;
            if (voice->pos >= voice->clip->len) {
                voice->clip = NULL;
                continue;
            }
            acc += voice->clip->data[voice->pos++];
        }
        dst[i] = clampS8(acc);
    }
}

void gbaAudioInit(void) {
    for (int v = 0; v < MAX_VOICES; v++) s_voices[v].clip = NULL;
    s_music.track = NULL;
    s_music.phase = 0;
    s_music.step = 0;
    s_nextHalf = 1;

    for (uint32_t i = 0; i < BUF_LEN; i++) {
        s_musicBuf[i] = 0;
        s_sfxBuf[i] = 0;
    }

    REG_SOUNDCNT_L = 0;
    REG_SOUNDCNT_H = (uint16_t)SOUNDCNT_H_INIT;
    REG_SOUNDCNT_X = 0x0080;  // maestro de sonido encendido

    // Timer 0 = reloj de muestreo. 16777216/1024 = 16384 Hz exacto, ver
    // tools/audio_gba_player.md para el porque no deja resto.
    REG_TM0CNT_L = (uint16_t)(65536u - (16777216u / HW_RATE));
    REG_TM0CNT_H = 0x0080;  // enable, sin prescaler ni cascada

    REG_DMA1SAD = s_musicBuf;
    REG_DMA1DAD = (void*)&REG_FIFO_A;
    REG_DMA1CNT_H = (uint16_t)DMA_SOUND_CTRL;

    REG_DMA2SAD = s_sfxBuf;
    REG_DMA2DAD = (void*)&REG_FIFO_B;
    REG_DMA2CNT_H = (uint16_t)DMA_SOUND_CTRL;
}

void gbaAudioSetMusic(const AudioTrack* track) {
    if (track == s_music.track) return;  // ya suena, no reiniciar
    s_music.track = track;
    s_music.phase = 0;
    s_music.step = track ? ((track->rate << 8) / HW_RATE) : 0;
}

void gbaAudioPlaySfx(const AudioClip* clip) {
    for (int v = 0; v < MAX_VOICES; v++) {
        if (s_voices[v].clip) continue;
        s_voices[v].clip = clip;
        s_voices[v].pos = 0;
        return;
    }
    // las 4 voces ocupadas: se pierde el efecto, igual que en escritorio
}

void gbaAudioVBlank(void) {
    int8_t* musicHalf = &s_musicBuf[s_nextHalf * HALF_LEN];
    int8_t* sfxHalf = &s_sfxBuf[s_nextHalf * HALF_LEN];

    fillMusic(musicHalf);
    fillSfx(sfxHalf);

    // ponytail: se asume una llamada por VBlank exacta; no se relee el
    // puntero de DMA para resincronizar si un ISR se retrasa mas de un
    // cuadro. Ver tools/audio_gba_player.md para el techo de este atajo y
    // como levantarlo (leer REG_DMA1SAD/REG_DMA2SAD en vez de asumir).
    s_nextHalf ^= 1;
}
