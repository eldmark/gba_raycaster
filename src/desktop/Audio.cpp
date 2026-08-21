#include "Audio.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>

namespace {

// El formato con el que estan grabados los siete WAV. Se abre el dispositivo
// exactamente asi para que SDL no tenga que convertir nada por muestra.
constexpr int RATE = 16000;
constexpr int CHANNELS = 1;

// Cuantos efectos pueden sonar a la vez. Con cuatro basta: disparo, impacto,
// recogida y un aviso, que es todo lo que puede coincidir en un frame.
constexpr int VOICES = 4;

struct Clip {
    Uint8* data = nullptr;
    Uint32 len = 0;
};

struct Voice {
    const Clip* clip = nullptr;
    Uint32 pos = 0;
};

struct Mixer {
    SDL_AudioDeviceID dev = 0;
    Clip sfx[Audio::SFX_COUNT];
    Clip track[Audio::TRACK_COUNT];
    Voice voices[VOICES];
    const Clip* music = nullptr;
    Uint32 musicPos = 0;
};

Mixer g;

// El callback corre en el hilo de audio de SDL. Solo lee los buffers de los
// clips y avanza posiciones; SDL_LockAudioDevice protege los cambios que hace
// el hilo del juego.
void mixCallback(void*, Uint8* stream, int bytes) {
    SDL_memset(stream, 0, bytes);

    if (g.music && g.music->len > 0) {
        Uint32 left = Uint32(bytes);
        Uint8* out = stream;
        // El tema se repite sin cortes: cuando se acaba el buffer se vuelve al
        // principio dentro del mismo callback, no en el siguiente.
        while (left > 0) {
            Uint32 chunk = g.music->len - g.musicPos;
            if (chunk > left) chunk = left;
            SDL_MixAudioFormat(out, g.music->data + g.musicPos, AUDIO_S16SYS, chunk,
                               SDL_MIX_MAXVOLUME / 3);
            g.musicPos += chunk;
            out += chunk;
            left -= chunk;
            if (g.musicPos >= g.music->len) g.musicPos = 0;
        }
    }

    for (Voice& v : g.voices) {
        if (!v.clip) continue;
        Uint32 chunk = v.clip->len - v.pos;
        if (chunk > Uint32(bytes)) chunk = Uint32(bytes);
        SDL_MixAudioFormat(stream, v.clip->data + v.pos, AUDIO_S16SYS, chunk,
                           SDL_MIX_MAXVOLUME);
        v.pos += chunk;
        if (v.pos >= v.clip->len) v.clip = nullptr;
    }
}

bool loadClip(const char* dir, const char* name, Clip& out) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s", dir, name);

    SDL_AudioSpec spec;
    if (!SDL_LoadWAV(path, &spec, &out.data, &out.len)) {
        std::fprintf(stderr, "audio: %s no se pudo cargar (%s)\n", path, SDL_GetError());
        return false;
    }
    // Se comprueba en vez de convertir: si algun WAV se regraba en otro formato
    // conviene enterarse aqui y no oir ruido blanco.
    if (spec.freq != RATE || spec.channels != CHANNELS || spec.format != AUDIO_S16LSB) {
        std::fprintf(stderr, "audio: %s no es mono 16 kHz 16 bits\n", path);
        SDL_FreeWAV(out.data);
        out = Clip{};
        return false;
    }
    return true;
}

}  // namespace

bool Audio::init(const char* dir) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "audio: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec want{};
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = CHANNELS;
    want.samples = 512;  // ~32 ms: suficientemente corto para que el disparo no se retrase
    want.callback = mixCallback;

    // Sin ALLOW_CHANGE: o el dispositivo acepta este formato o SDL convierte por
    // dentro, pero el callback siempre recibe lo que se pidio.
    g.dev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (g.dev == 0) {
        std::fprintf(stderr, "audio: sin dispositivo (%s)\n", SDL_GetError());
        return false;
    }

    loadClip(dir, "shot.wav", g.sfx[Shot]);
    loadClip(dir, "damage_static.wav", g.sfx[Damage]);
    loadClip(dir, "pickup_item.wav", g.sfx[Pickup]);
    loadClip(dir, "level_up.wav", g.sfx[LevelUp]);
    loadClip(dir, "victory_endgame.wav", g.sfx[Victory]);
    loadClip(dir, "main_theme.wav", g.track[Menu]);
    loadClip(dir, "asault_theme.wav", g.track[Assault]);

    SDL_PauseAudioDevice(g.dev, 0);
    ready_ = true;
    return true;
}

void Audio::play(Sfx sfx) {
    if (!ready_ || !g.sfx[sfx].data) return;
    SDL_LockAudioDevice(g.dev);
    // Se busca un canal libre; si los cuatro estan ocupados el efecto se pierde
    // en vez de cortar uno que ya suena.
    for (Voice& v : g.voices) {
        if (v.clip) continue;
        v.clip = &g.sfx[sfx];
        v.pos = 0;
        break;
    }
    SDL_UnlockAudioDevice(g.dev);
}

void Audio::setTrack(Track track) {
    if (!ready_) return;
    const Clip* next = (track == None) ? nullptr : &g.track[track];
    if (next && !next->data) next = nullptr;
    if (next == g.music) return;  // ya suena: no se reinicia

    SDL_LockAudioDevice(g.dev);
    g.music = next;
    g.musicPos = 0;
    SDL_UnlockAudioDevice(g.dev);
}

void Audio::shutdown() {
    if (g.dev) {
        SDL_CloseAudioDevice(g.dev);
        g.dev = 0;
    }
    for (Clip& c : g.sfx) {
        if (c.data) SDL_FreeWAV(c.data);
        c = Clip{};
    }
    for (Clip& c : g.track) {
        if (c.data) SDL_FreeWAV(c.data);
        c = Clip{};
    }
    g = Mixer{};
    ready_ = false;
}
