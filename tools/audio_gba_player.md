# Reproductor de audio para GBA (fase 7)

**Este documento describe el reproductor que se envio.** El codigo esta en
`src/gba/Audio.cpp` y `src/gba/Audio.h`, enlazado desde `Makefile.gba`. Hubo una
version de referencia previa en `tools/audio_player.c/.h` que ya no existe: al
aterrizarla sobre el esqueleto real cambiaron dos decisiones de fondo -la tasa
de muestreo y como se realimenta el DMA-, y mantenerla al lado habria dejado dos
disenos distintos con el mismo nombre.

## Por que DirectSound y no los 4 canales DMG

La GBA tiene dos motores de sonido: 4 canales DMG (heredados de Game Boy
Color, ondas cuadradas/ruido/onda programable) y 2 canales DirectSound
(FIFO A/B, PCM de 8 bits arbitrario). El juego ya tiene 7 WAV grabados —
musica con instrumentos reales y efectos con textura de disparo/estatico— eso
no se sintetiza razonablemente con 4 canales DMG. DirectSound reproduce PCM
horneado tal cual: es la unica opcion que no significa re-componer el audio.

## Reloj de muestreo: timer 0, una sola tasa para todo

DirectSound no tiene generador de reloj propio: cada FIFO se vacia al ritmo del
timer que le marque `SOUNDCNT_H`, y solo puede ser el **timer 0 o el 1**. Se usa
el timer 0 para los dos canales, a una tasa unica de **10512 Hz**:

```
reload = 65536 - 1596       (16777216 / 1596 = 10512 Hz)
```

La razon de que sea 1596 y no otra cosa es la cadencia contra el video: un
cuadro de la consola son **280.896 ciclos** y `280896 / 1596 = 176` **exacto**.
En cada VBlank se consumen 176 muestras justas, asi que el buffer se puede
rellenar y el DMA rearmar una vez por VBlank sin acumular deriva. A los 16384 Hz
que proponia el diseno original salen 274,3125 muestras por cuadro, y esa
fraccion hay que ir arrastrandola frame a frame.

Y como `tools/audio_bake.py` hornea **musica y efectos a esa misma tasa**, no
queda remuestreador en tiempo real: reproducir es copiar bytes. El acumulador de
fase 8.8 que describia el diseno original desaparecio con el.

Efecto colateral en el resto del port: el contador de ciclos de
`src/gba/Debug.h` tuvo que mudarse de TM0+TM1 a **TM2+TM3**. El audio no puede
usar otros timers; el contador si.

## FIFO A = musica, FIFO B = efectos, la suma la hace el hardware

- **DMA1 -> FIFO A**: solo el canal de musica, copiada tal cual del asset. Una
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

## Buffer doble en EWRAM, DMA rearmado en cada VBlank

Cada FIFO se alimenta con un buffer de dos mitades de 176 muestras (352 bytes
por canal, 704 en total). 176 es multiplo de 4, que es lo que necesita un DMA de
32 bits: el hardware ignora los dos bits bajos de la direccion y una mitad
desalineada se leeria corrida.

El DMA va en modo "Special/Sound timing": destino fijo, fuente incremental, 32
bits, repetir. Quien dispara cada transferencia es la senal de "FIFO con hambre"
del propio hardware de sonido, no el timer directamente.

**El punto que el diseno original tenia mal**: no basta con armar el canal una
vez. El hardware no envuelve el puntero de origen solo -con *Repeat* recarga la
cuenta pero sigue leyendo hacia adelante-, asi que un canal armado una vez se
sale del buffer a los pocos cuadros y lo que sale por el altavoz es memoria.
`onVBlank()` apaga el canal, lo apunta a la mitad que se lleno en el VBlank
anterior y lo vuelve a encender; solo despues rellena la otra mitad. Al reves se
estaria escribiendo encima de lo que el hardware esta leyendo en ese momento.

Rearmar cada cuadro es ademas lo que mantiene el sonido enganchado al video pase
lo que pase con la duracion del frame de juego: el motor va a 12-15 fps, pero
este ISR corre a 60 Hz, colgado de `irqSet(IRQ_VBLANK, ...)` de libgba.

## Coste, y por que el mezclador vive en IWRAM

La primera version costaba **195.000 ciclos por frame de juego**. Corre cuatro o
cinco veces por cada frame del motor, y desde la ROM salia mas caro que dibujar
todos los sprites. Dos arreglos lo bajaron a unos **11.000**:

- `Audio.o` a IWRAM compilado en ARM, la misma leccion que el resto del port.
- Sacar del bucle por muestra lo que no cambia dentro de el: las voces vivas se
  recogen antes de entrar, y los casos de cero voces y de una sola -que son el
  99% del tiempo- no pasan por la suma ni por la saturacion.

## Un registro mal escrito, y como se vio

`REG_DMA2CNT_H` estaba puesto en `0x0CE`. Los registros de DMA van de doce en
doce bytes desde `0x0BC`, asi que `0x0CE` cae en la mitad alta de `DMA2DAD` y no
en el control del canal: el canal de efectos nunca llegaba a encenderse y el
cartucho sonaba con musica y sin un solo disparo. La direccion correcta es
`0x0D2`.

Se vio corriendo `mgba -l 15` y contando las lineas "Starting DMA": 1157 del
canal 1 y **cero** del canal 2. Es la clase de fallo que no da error de
compilacion ni cuelga nada, y que escuchando por encima se confunde con "los
efectos suenan bajito".

## API (`src/gba/Audio.h`)

Es la MISMA que la de escritorio (`src/desktop/Audio.h`), a proposito: el bucle
principal de cada plataforma hace las mismas llamadas en el mismo orden, y la
capa de juego sigue sin saber que existe el sonido.

```cpp
bool init();
void shutdown();
void play(Sfx sfx);        // Shot, Damage, Pickup, LevelUp, Victory
void setTrack(Track track); // None, Menu, Assault
```

Los datos son los arrays que hornea `tools/audio_bake.py` en
`tools/audio_baked/*.c`, declarados todos desde `audio_assets.h`. Se compilan
con g++ como el resto (g++ trata un `.c` como C++), asi que no hace falta
ningun `extern "C"` de por medio.

## Presupuesto de EWRAM

```
buffer musica (2 x 176 s8)   352 B
buffer efectos (2 x 176 s8)  352 B
estado de 4 voces             48 B
estado del canal de musica    12 B
--------------------------------
total                        ~0,8 KB
```

Muy por debajo de los ~4 KB que estimo la fase 7. Donde si pesa el audio es en
la ROM: 2,06 MB de PCM horneado, que llevan el cartucho de 79 KB a 2,14 MB.
