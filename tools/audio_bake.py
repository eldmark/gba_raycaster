#!/usr/bin/env python3
"""
Hornea los WAV de public/audio a PCM de 8 bits con signo para la GBA.

Por que asi: la GBA reproduce audio por DirectSound (canales A/B por FIFO,
alimentados por DMA) que solo entiende PCM de 8 bits con signo mono. No hay
decodificador de hardware, asi que cualquier formato mas chico (ADPCM, etc.)
necesitaria un decodificador en tiempo real -- mas ciclos de CPU, justo lo que
el resto del plan de port esta peleando por ahorrar. 8 bits sin comprimir es
la opcion que no le cuesta nada a la CPU en tiempo de reproduccion.

Por que ffmpeg y no un resampler casero: swresample/soxr (activado en este
ffmpeg) hace un remuestreo con filtro de banda limitada correcto. Escribir un
remuestreador lineal a mano metería aliasing que un oido no distingue del
"suena raro" que se supone que estamos evitando -- reinventar esto no compra
nada.

Tasa elegida: 10512 Hz para TODO, musica y efectos.

Es una de las tasas clasicas de GBA (reload de timer = 65536-1596), y la
razon de que sea una sola y sea esta esta en como se alimenta la FIFO: un
cuadro de video de la consola son 280.896 ciclos y 280896/1596 = 176 EXACTO.
En cada VBlank se consumen exactamente 176 muestras, ni una mas ni una
menos, asi que el reproductor puede rearmar el DMA cada VBlank sobre un
buffer de 176 sin acumular deriva. A 16384 Hz salen 274,3125 muestras por
cuadro, y esa fraccion hay que ir arrastrandola.

Que sea la MISMA tasa para efectos y musica ahorra ademas el remuestreo en
tiempo real que el diseno original ponia en el mezclador: si el asset ya
esta a la tasa de hardware, reproducirlo es copiar bytes. Coste: los efectos
pierden brillo respecto a los 16 kHz originales (5,2 kHz de ancho de banda
en vez de 8), que en el altavoz de una GBA no es donde se pierde la partida.
"""
import json
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUDIO_DIR = ROOT / "public" / "audio"
OUT_DIR = ROOT / "tools" / "audio_baked"

SFX_RATE = 10512
MUSIC_RATE = 10512

# (nombre de archivo, tasa objetivo, nombre de simbolo C, es musica)
ASSETS = [
    ("shot.wav", SFX_RATE, "shot", False),
    ("damage_static.wav", SFX_RATE, "damage", False),
    ("pickup_item.wav", SFX_RATE, "pickup", False),
    ("level_up.wav", SFX_RATE, "level_up", False),
    ("victory_endgame.wav", SFX_RATE, "victory", False),
    ("main_theme.wav", MUSIC_RATE, "main_theme", True),
    ("asault_theme.wav", MUSIC_RATE, "assault_theme", True),
]


def read_wav_stats(path: Path):
    """Duracion y RMS normalizado (-1..1) de un WAV PCM16 mono."""
    with wave.open(str(path), "rb") as w:
        rate = w.getframerate()
        nframes = w.getnframes()
        raw = w.readframes(nframes)
    samples = struct.unpack("<%dh" % (len(raw) // 2), raw)
    if not samples:
        return 0.0, 0.0
    sumsq = sum(s * s for s in samples)
    rms = (sumsq / len(samples)) ** 0.5 / 32768.0
    duration = nframes / float(rate)
    return duration, rms


def raw_s8_stats(path: Path, rate: int):
    """Duracion y RMS normalizado (-1..1) de un PCM8 con signo crudo."""
    data = path.read_bytes()
    samples = struct.unpack("<%db" % len(data), data)
    if not samples:
        return 0.0, 0.0
    sumsq = sum(s * s for s in samples)
    rms = (sumsq / len(samples)) ** 0.5 / 128.0
    duration = len(samples) / float(rate)
    return duration, rms


def resample_to_s8(src: Path, rate: int, dst: Path):
    """ffmpeg + soxr: remuestrea a `rate`, mono, PCM8 con signo, sin cabecera."""
    subprocess.run(
        [
            "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
            "-i", str(src),
            "-af", "aresample=resampler=soxr",
            "-ar", str(rate),
            "-ac", "1",
            "-f", "s8",
            str(dst),
        ],
        check=True,
    )


def emit_c_array(symbol: str, data: bytes, rate: int, out_c: Path, out_h: Path):
    guard = f"AUDIO_{symbol.upper()}_H"
    out_h.write_text(
        f"""#ifndef {guard}
#define {guard}

// Generado por tools/audio_bake.py. No editar a mano: si hace falta cambiar
// la tasa o el archivo fuente, se cambia ASSETS en el script y se regenera.
#include <stdint.h>

extern const int8_t {symbol}_pcm[];
extern const uint32_t {symbol}_pcm_len;
extern const uint32_t {symbol}_pcm_rate;

#endif // {guard}
"""
    )

    lines = []
    for i in range(0, len(data), 20):
        chunk = data[i:i + 20]
        lines.append(",".join(str(b if b < 128 else b - 256) for b in chunk))
    body = ",\n".join(lines)

    out_c.write_text(
        f"""#include "{symbol}.h"

// Generado por tools/audio_bake.py a partir de public/audio/*.wav.
// {len(data)} muestras a {rate} Hz, PCM de 8 bits con signo.
const int8_t {symbol}_pcm[{len(data)}] = {{
{body}
}};

const uint32_t {symbol}_pcm_len = {len(data)}u;
const uint32_t {symbol}_pcm_rate = {rate}u;
"""
    )


def main():
    if shutil.which("ffmpeg") is None:
        print("hace falta ffmpeg en el PATH", file=sys.stderr)
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    report = []
    total_bytes = 0

    for fname, rate, symbol, is_music in ASSETS:
        src = AUDIO_DIR / fname
        if not src.exists():
            print(f"falta {src}", file=sys.stderr)
            return 1

        dur_before, rms_before = read_wav_stats(src)

        raw_path = OUT_DIR / f"{symbol}.raw"
        resample_to_s8(src, rate, raw_path)
        data = raw_path.read_bytes()

        dur_after, rms_after = raw_s8_stats(raw_path, rate)

        emit_c_array(symbol, data, rate, OUT_DIR / f"{symbol}.c", OUT_DIR / f"{symbol}.h")
        raw_path.unlink()  # el .raw solo era un paso intermedio de verificacion

        total_bytes += len(data)
        report.append({
            "file": fname,
            "symbol": symbol,
            "kind": "musica" if is_music else "efecto",
            "rate_hz": rate,
            "duration_before_s": round(dur_before, 3),
            "duration_after_s": round(dur_after, 3),
            "duration_delta_pct": round(100.0 * (dur_after - dur_before) / dur_before, 3) if dur_before else 0.0,
            "rms_before": round(rms_before, 5),
            "rms_after": round(rms_after, 5),
            "bytes_before": src.stat().st_size,
            "bytes_after": len(data),
        })

    # Agregador con todas las declaraciones, para incluir uno solo desde el
    # reproductor en vez de siete headers sueltos.
    agg = OUT_DIR / "audio_assets.h"
    includes = "\n".join(f'#include "{a["symbol"]}.h"' for a in report)
    agg.write_text(
        f"""#ifndef AUDIO_ASSETS_H
#define AUDIO_ASSETS_H

// Punto unico de inclusion para todos los assets horneados.
{includes}

#endif // AUDIO_ASSETS_H
"""
    )

    print(json.dumps({"assets": report, "total_bytes": total_bytes}, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
