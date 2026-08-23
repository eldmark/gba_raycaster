#include "Audio.h"

#include <gba_interrupt.h>

#include <stdint.h>

#include "audio_assets.h"

// Reproductor de DirectSound. El diseno viene de tools/audio_gba_player.md;
// aqui van las dos cosas que cambiaron al aterrizarlo sobre el esqueleto real.
//
// 1. UNA sola tasa de hardware, 10512 Hz, para musica y efectos.
//    El documento proponia 16384 Hz y remuestrear la musica al vuelo. El
//    problema es la cadencia: un cuadro de video son 280.896 ciclos y
//    280896/1024 = 274,3125 muestras. Esa fraccion hay que arrastrarla cuadro
//    a cuadro. Con reload 1596 (10512 Hz) salen 280896/1596 = 176 EXACTO, asi
//    que en cada VBlank se consumen exactamente 176 muestras y el buffer se
//    puede rearmar sin deriva. Como ademas tools/audio_bake.py hornea todo a
//    esa misma tasa, reproducir es copiar bytes: no queda remuestreador.
//
// 2. El DMA se REARMA en cada VBlank en vez de dejarlo correr.
//    El hardware no envuelve el puntero fuente solo: con Repeat activo
//    recarga la cuenta pero sigue leyendo hacia adelante, asi que un DMA
//    armado una vez se sale del buffer a los pocos cuadros. Se apaga y se
//    vuelve a armar apuntando a la mitad que toca, que es lo que ademas
//    mantiene el sonido enganchado al video pase lo que pase con la duracion
//    del frame de juego: el motor va a 12-15 fps, pero este ISR corre a 60 Hz.

namespace {

constexpr uint32_t kRegBase = 0x04000000u;  // no REG_BASE: gba_base.h ya lo define como macro

#define SND_REG16(off) (*reinterpret_cast<volatile uint16_t*>(kRegBase + (off)))
volatile uint16_t& REG_SOUNDCNT_L = SND_REG16(0x080);
volatile uint16_t& REG_SOUNDCNT_H = SND_REG16(0x082);
volatile uint16_t& REG_SOUNDCNT_X = SND_REG16(0x084);
volatile uint16_t& REG_TM0CNT_L = SND_REG16(0x100);
volatile uint16_t& REG_TM0CNT_H = SND_REG16(0x102);
volatile uint16_t& REG_DMA1CNT_H = SND_REG16(0x0C6);
// 0x0D2 y no 0x0CE: los registros de DMA van de doce en doce bytes
// (DMA1 en 0xBC, DMA2 en 0xC8, DMA3 en 0xD4), asi que 0x0CE cae en la mitad
// alta de DMA2DAD, no en su control. Escribiendo ahi el canal de efectos
// nunca llegaba a encenderse y el juego sonaba con musica pero sin disparos.
volatile uint16_t& REG_DMA2CNT_H = SND_REG16(0x0D2);
#undef SND_REG16

volatile const void** const REG_DMA1SAD =
    reinterpret_cast<volatile const void**>(kRegBase + 0x0BC);
volatile void** const REG_DMA1DAD = reinterpret_cast<volatile void**>(kRegBase + 0x0C0);
volatile const void** const REG_DMA2SAD =
    reinterpret_cast<volatile const void**>(kRegBase + 0x0C8);
volatile void** const REG_DMA2DAD = reinterpret_cast<volatile void**>(kRegBase + 0x0CC);

void* const kFifoA = reinterpret_cast<void*>(kRegBase + 0x0A0);
void* const kFifoB = reinterpret_cast<void*>(kRegBase + 0x0A4);

// SOUNDCNT_H: DirectSound A y B al 100%, los dos a ambos oidos, los dos
// tomando su reloj del timer 0, y reset de las dos FIFO al arrancar.
constexpr uint16_t kSoundCntH = (1u << 2) | (1u << 3) |     // A y B al 100%
                                (1u << 8) | (1u << 9) |     // A a izquierda y derecha
                                (0u << 10) | (1u << 11) |   // A usa timer 0, reset FIFO A
                                (1u << 12) | (1u << 13) |   // B a izquierda y derecha
                                (0u << 14) | (1u << 15);    // B usa timer 0, reset FIFO B

// DMA en modo "Special/Sound timing": destino fijo (la FIFO), fuente
// incremental, palabras de 32 bits, repetir. Quien dispara cada transferencia
// es la senal de "FIFO con hambre" del propio hardware de sonido, no la cuenta
// del DMA ni el timer directamente.
constexpr uint16_t kDmaSound = (2u << 5) |    // destino fijo
                               (0u << 7) |    // fuente incremental
                               (1u << 9) |    // repetir
                               (1u << 10) |   // 32 bits
                               (3u << 12) |   // timing especial (sonido)
                               (1u << 15);    // enable

constexpr uint32_t kHwRate = 10512u;
// 16777216 / 10512,03 = 1596. El reload es 65536 - 1596.
constexpr uint32_t kTimerPeriod = 1596u;
constexpr uint16_t kTimerReload = uint16_t(65536u - kTimerPeriod);
static_assert(16777216u / kTimerPeriod == kHwRate, "el reload no da la tasa horneada");
// 280896 ciclos de un cuadro / 1596 = 176 muestras exactas por VBlank.
constexpr int kFrameSamples = 176;

// Multiplo de 4: el DMA transfiere palabras de 32 bits e ignora los dos bits
// bajos de la direccion, asi que las dos mitades tienen que caer alineadas.
static_assert(kFrameSamples % 4 == 0, "las mitades del buffer deben alinearse a 4");

alignas(4) int8_t s_musicBuf[kFrameSamples * 2];
alignas(4) int8_t s_sfxBuf[kFrameSamples * 2];

// Mitad que se esta reproduciendo ahora. La otra es la que se rellena.
uint8_t s_half = 0;

struct MusicState {
    const int8_t* data = nullptr;
    uint32_t len = 0;
    uint32_t pos = 0;
};

// Cuatro voces, el mismo limite que el mezclador de escritorio: si estan todas
// ocupadas se pierde el efecto nuevo en vez de cortar uno que ya suena.
constexpr int kMaxVoices = 4;
struct Voice {
    const int8_t* data = nullptr;
    uint32_t len = 0;
    uint32_t pos = 0;
};

MusicState s_music;
Voice s_voices[kMaxVoices];
Audio::Track s_track = Audio::None;
bool s_ready = false;

inline int8_t clampS8(int v) {
    if (v > 127) return 127;
    if (v < -128) return -128;
    return int8_t(v);
}

void fillMusic(int8_t* dst) {
    if (!s_music.data || s_music.len == 0) {
        for (int i = 0; i < kFrameSamples; ++i) dst[i] = 0;
        return;
    }
    // El asset ya esta a la tasa de hardware: no hay acumulador de fase ni
    // remuestreo. Y la vuelta al principio se resuelve por tramos y no
    // comprobandola en cada muestra: el bucle de dentro queda sin ninguna
    // rama, que es lo que importa en un ISR que corre 60 veces por segundo.
    uint32_t pos = s_music.pos;
    const int8_t* src = s_music.data;
    const uint32_t len = s_music.len;
    int done = 0;
    while (done < kFrameSamples) {
        if (pos >= len) pos = 0;
        int chunk = kFrameSamples - done;
        const uint32_t remain = len - pos;
        if (uint32_t(chunk) > remain) chunk = int(remain);
        const int8_t* from = src + pos;
        int8_t* to = dst + done;
        for (int i = 0; i < chunk; ++i) to[i] = from[i];
        pos += uint32_t(chunk);
        done += chunk;
    }
    s_music.pos = pos;
}

void fillSfx(int8_t* dst) {
    // Las voces vivas se recogen ANTES del bucle de muestras. Mirando los
    // cuatro huecos por muestra se pagaban cuatro comprobaciones 176 veces
    // aunque no sonara nada, y no sonar nada es el caso normal: los efectos
    // duran decimas y la partida entera transcurre entre uno y otro.
    Voice* active[kMaxVoices];
    int n = 0;
    for (int v = 0; v < kMaxVoices; ++v) {
        Voice& voice = s_voices[v];
        if (!voice.data) continue;
        if (voice.pos >= voice.len) {
            voice.data = nullptr;
            continue;
        }
        active[n++] = &voice;
    }

    if (n == 0) {
        for (int i = 0; i < kFrameSamples; ++i) dst[i] = 0;
        return;
    }

    if (n == 1) {
        // Una sola voz: no hay nada que sumar ni que saturar, es una copia.
        Voice* voice = active[0];
        const uint32_t remain = voice->len - voice->pos;
        int copy = kFrameSamples;
        if (uint32_t(copy) > remain) copy = int(remain);
        const int8_t* from = voice->data + voice->pos;
        for (int i = 0; i < copy; ++i) dst[i] = from[i];
        for (int i = copy; i < kFrameSamples; ++i) dst[i] = 0;
        voice->pos += uint32_t(copy);
        return;
    }

    for (int i = 0; i < kFrameSamples; ++i) {
        int acc = 0;
        for (int k = 0; k < n; ++k) {
            Voice* voice = active[k];
            if (voice->pos < voice->len) acc += voice->data[voice->pos++];
        }
        dst[i] = clampS8(acc);
    }
}

void rearm(volatile const void** sad, volatile uint16_t& cnt, const int8_t* src,
           void* fifo, volatile void** dad) {
    cnt = 0;
    // Una lectura de por medio: apagar y volver a encender el canal en dos
    // escrituras seguidas no siempre le da al DMA el ciclo que necesita para
    // soltar el bus.
    const uint16_t settle = cnt;
    (void)settle;
    *sad = src;
    *dad = fifo;
    cnt = kDmaSound;
}

void onVBlank() {
    if (!s_ready) return;

    // Primero se apunta el DMA a la mitad que se lleno en el VBlank anterior,
    // y solo despues se rellena la otra. Al reves se estaria escribiendo
    // encima de lo que el hardware esta leyendo en ese mismo momento.
    rearm(REG_DMA1SAD, REG_DMA1CNT_H, s_musicBuf + s_half * kFrameSamples, kFifoA,
          REG_DMA1DAD);
    rearm(REG_DMA2SAD, REG_DMA2CNT_H, s_sfxBuf + s_half * kFrameSamples, kFifoB,
          REG_DMA2DAD);

    s_half ^= 1;
    fillMusic(s_musicBuf + s_half * kFrameSamples);
    fillSfx(s_sfxBuf + s_half * kFrameSamples);
}

// Los assets horneados, en el mismo orden que los enum de Audio.
struct Clip {
    const int8_t* data;
    uint32_t len;
};

Clip sfxClip(Audio::Sfx sfx) {
    switch (sfx) {
        case Audio::Shot: return {shot_pcm, shot_pcm_len};
        case Audio::Damage: return {damage_pcm, damage_pcm_len};
        case Audio::Pickup: return {pickup_pcm, pickup_pcm_len};
        case Audio::LevelUp: return {level_up_pcm, level_up_pcm_len};
        case Audio::Victory: return {victory_pcm, victory_pcm_len};
        default: return {nullptr, 0};
    }
}

}  // namespace

bool Audio::init() {
    for (int v = 0; v < kMaxVoices; ++v) s_voices[v].data = nullptr;
    s_music = MusicState{};
    s_track = None;
    s_half = 0;
    for (int i = 0; i < kFrameSamples * 2; ++i) {
        s_musicBuf[i] = 0;
        s_sfxBuf[i] = 0;
    }

    REG_SOUNDCNT_L = 0;  // los 4 canales DMG apagados: aqui no se usan
    REG_SOUNDCNT_H = kSoundCntH;
    REG_SOUNDCNT_X = 0x0080;  // maestro de sonido encendido

    REG_TM0CNT_L = kTimerReload;
    REG_TM0CNT_H = 0x0080;  // enable, sin prescaler ni cascada

    // El ISR de VBlank es lo unico que mantiene el buffer lleno. Sin el, el
    // DMA se sale del buffer y lo que sale por el altavoz es memoria.
    irqInit();
    irqSet(IRQ_VBLANK, onVBlank);
    irqEnable(IRQ_VBLANK);

    s_ready = true;
    return true;
}

void Audio::shutdown() {
    s_ready = false;
    REG_DMA1CNT_H = 0;
    REG_DMA2CNT_H = 0;
    REG_SOUNDCNT_X = 0;
}

void Audio::play(Sfx sfx) {
    const Clip clip = sfxClip(sfx);
    if (!clip.data || clip.len == 0) return;
    for (int v = 0; v < kMaxVoices; ++v) {
        if (s_voices[v].data) continue;
        // El ISR lee estos campos: se deja `data` para el final, que es el que
        // hace que la voz cuente como activa.
        s_voices[v].len = clip.len;
        s_voices[v].pos = 0;
        s_voices[v].data = clip.data;
        return;
    }
    // Las cuatro ocupadas: se pierde, igual que en escritorio.
}

void Audio::setTrack(Track track) {
    if (track == s_track) return;  // ya suena, no reiniciar
    s_track = track;

    // El orden importa: el ISR puede saltar en mitad de esto. Poniendo la
    // longitud a cero PRIMERO, lo peor que puede pasar es un cuadro de
    // silencio; al reves -puntero nuevo con longitud vieja- se leeria fuera
    // del array si la pista nueva es mas corta que la anterior.
    s_music.len = 0;
    switch (track) {
        case Menu:
            s_music.data = main_theme_pcm;
            s_music.pos = 0;
            s_music.len = main_theme_pcm_len;
            break;
        case Assault:
            s_music.data = assault_theme_pcm;
            s_music.pos = 0;
            s_music.len = assault_theme_pcm_len;
            break;
        default:
            s_music.data = nullptr;
            s_music.pos = 0;
            break;
    }
}
