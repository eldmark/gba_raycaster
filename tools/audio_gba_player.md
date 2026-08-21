# Reproductor de audio para GBA (fase 7)

Diseno del reproductor. El codigo de referencia esta en `tools/audio_player.h` y
`tools/audio_player.c`, autonomo: no incluye nada de `src/engine`, `src/game` ni
`src/desktop`, y no toca ningun Makefile. Es codigo de referencia, no esta
enlazado en ninguna ROM todavia porque el esqueleto GBA (fase 3) no existe en
este worktree. Queda listo para que quien construya el esqueleto lo enganche.

## Por que DirectSound y no los 4 canales DMG

La GBA tiene dos motores de sonido: 4 canales DMG (heredados de Game Boy
Color, ondas cuadradas/ruido/onda programable) y 2 canales DirectSound
(FIFO A/B, PCM de 8 bits arbitrario). El juego ya tiene 7 WAV grabados —
musica con instrumentos reales y efectos con textura de disparo/estatico— eso
no se sintetiza razonablemente con 4 canales DMG. DirectSound reproduce PCM
horneado tal cual: es la unica opcion que no significa re-componer el audio.

## Reloj de muestreo: timer 0, una sola tasa de hardware

El hardware de DirectSound no tiene su propio generador de reloj: cada FIFO
se vacia al ritmo que le marque uno de los timers (`DSOUNDCTRL_ATIMER`,
`DSOUNDCTRL_BTIMER`), y ambos FIFO pueden apuntar al mismo timer. Se usa
**timer 0 para los dos**, a una tasa unica de reproduccion de hardware:

```
HW_RATE = 16384 Hz
reload  = 65536 - (16777216 / HW_RATE) = 65536 - 1024 = 64512
```

16384 Hz se eligio porque `16777216 / 1024 = 16384` exacto: el reload no deja
resto, asi que el timer no arrastra error de fase contra el reloj de la
consola frame tras frame. Es tambien la tasa a la que se hornearon los
efectos (`tools/audio_bake.py`), asi que se copian a la FIFO tal cual, sin
convertir nada en tiempo real.

La musica se horneo a 10512 Hz para ahorrar ROM (ver `docs/port.md` fase 7 y
el reporte de `audio_bake.py`). Para no duplicar buffers a dos tasas de
hardware, el canal de musica se remuestrea en el momento de mezclar con un
acumulador de fase de punto fijo (8.8): un `paso = (10512<<8)/16384` que se
suma por muestra de salida y el indice de lectura es `acumulador >> 8`
(vecino mas cercano, sin interpolar). Es la misma tecnica que usan los
reproductores de tracker de la escena homebrew (p.ej. Maxmod) para no
necesitar una tasa de hardware por pista. Coste: una suma, un shift y un
acceso a array por muestra de musica — barato comparado con los ~200.000
ciclos que ya se gastan en el bucle de pared.

## FIFO A = musica, FIFO B = efectos, la suma la hace el hardware

- **DMA1 -> FIFO A**: solo el canal de musica (resampleado como arriba). Una
  sola fuente, sin mezclar en software.
- **DMA2 -> FIFO B**: la mezcla en software de hasta 4 voces de efectos
  simultaneos (disparo, dano, recogida, aviso — el mismo limite de 4 que ya
  usa el mezclador de escritorio en `src/desktop/Audio.cpp`). Se suman en un
  acumulador de 16 bits y se recortan a `[-128,127]` antes de escribir el
  byte que sale por DMA.
- La suma final de musica + efectos la hace el **hardware** de audio de la
  GBA al combinar ambos FIFO en el mismo bus analogico — no hace falta un
  paso de mezcla adicional en software para juntar ambos canales.

Esto respeta el pedido: "mezcla de un canal de musica mas efectos", pero solo
donde hace falta software (varios efectos a la vez comparten un FIFO); mezclar
musica+efectos es gratis porque ya lo hace el chip.

## Buffer doble en EWRAM, DMA en modo "Special" (FIFO)

Cada FIFO se alimenta con un buffer circular de dos mitades
(`HALF_LEN = 320` muestras, multiplo de 4 para transferencias de 32 bits):

```
[ mitad A (320 s8) | mitad B (320 s8) ]   <- 640 bytes por canal, 1280 total
```

El DMA se arma una vez en modo "Special/Sound Timing": `DestAddrCtrl=Fixed`,
`SrcAddrCtrl=Increment`, `Size=32bit`, `Repeat=1`, disparado por la senal de
"FIFO necesita datos" del propio hardware de sonido, no por el timer
directamente ni por VBlank. El timer 0 es lo que hace que esa senal llegue a
la tasa correcta.

Como el DMA nunca se detiene solo (sigue incrementando el puntero fuente
mientras el juego corra), hace falta re-sincronizarlo:

- `gbaAudioVBlank()` se llama una vez por VBlank (desde el ISR de VBlank del
  esqueleto GBA, cuando exista). Rellena la mitad que **no** se esta
  reproduciendo con muestras nuevas mezcladas.
- Cuando el DMA llega al final del buffer de 640 bytes hay que devolver el
  puntero fuente al principio a mano (escribir `REG_DMAxSAD` y re-armar el
  canal) porque el hardware no envuelve el puntero solo.

> ponytail: la version de referencia asume que `gbaAudioVBlank()` se llama
> exactamente una vez por VBlank (59,7 Hz) y no lee de vuelta el puntero de
> DMA (`REG_DMA1SAD`/`REG_DMA2SAD`) para resincronizarse si un IRQ se retrasa
> mas de un cuadro. Si en el esqueleto real algun ISR mas largo llega a saltar
> un VBlank, el audio se desincroniza brevemente (glitch de un frame, no un
> crash). Subsanar leyendo el puntero de DMA real en vez de asumir la
> cadencia, si hace falta mas robustez que la que da un homebrew casual.

## API de referencia (`tools/audio_player.h`)

```c
void gbaAudioInit(void);
void gbaAudioSetMusic(const AudioTrack* track); // NULL detiene la musica
void gbaAudioPlaySfx(const AudioClip* clip);    // se pierde si las 4 voces estan ocupadas
void gbaAudioVBlank(void);                      // llamar una vez por VBlank
```

`AudioTrack` y `AudioClip` son punteros a los arrays horneados por
`tools/audio_bake.py` (`tools/audio_baked/*.c/.h`, incluidos todos desde
`audio_assets.h`).

## Presupuesto de EWRAM

```
buffer musica (2 x 320 s8)   640 B
buffer efectos (2 x 320 s8)  640 B
estado de 4 voces de efectos  32 B  (puntero + posicion + volumen por voz)
estado del canal de musica     8 B  (puntero + acumulador de fase)
--------------------------------
total                       ~1.3 KB
```

Bastante por debajo de los ~4 KB que estimo la fase 7 del plan — sobra margen
si hiciera falta engordar el buffer para tolerar un ISR mas lento.

## Que falta para integrar (fuera de este alcance)

1. Enganchar `gbaAudioInit()` al arranque y `gbaAudioVBlank()` al ISR de
   VBlank del `Platform` de GBA — no existe todavia en este worktree (fase 3,
   la construye otro agente en paralelo).
2. Reemplazar `src/desktop/Audio.h`/`.cpp` por un `Audio` de GBA que llame a
   esta API (mismo patron que `Platform`: la capa de juego no sabe que existe
   el sonido, solo llama a `play(Sfx)` / `setTrack(Track)`).
3. Enlazar `tools/audio_baked/*.c` en el build de GBA (no en el de escritorio:
   ese sigue leyendo los `.wav` con SDL, sin tocar).
