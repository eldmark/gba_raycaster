# VioletHat FPS para Game Boy Advance

Motor de raycasting con DDA, escrito desde cero en C++, sin librerías de
raycasting. No eres alguien perdido en un laberinto: eres un programa
infiltrándose en un archivo muerto, y los guardianes que quedan dentro llevan
demasiado tiempo solos.

![Pasillo del archivo](public/screenshot.png)

## Video: demo del último nivel

### ▶ [https://youtu.be/6PK_r0pvREs](https://youtu.be/6PK_r0pvREs)

El último archivo entero corriendo en la consola: el núcleo centinela, la cámara
sellada y la extracción. Haz clic en la pantalla de título para abrirlo.

|                                  |                          |
| -------------------------------- | ------------------------ |
| ![Guardián](public/guardian.png) | [![Título](public/title.png)](https://youtu.be/6PK_r0pvREs) |

Los seis materiales de pared y todos los sprites, tal como los construye
`src/engine/Textures.h`. No hay archivos de imagen: se dibujan con código.

![Materiales y sprites](public/materials.png)

## Estado

Corre en Linux y **arranca en Game Boy Advance**. `make -f Makefile.gba` deja un
cartucho de 2,1 MB (dos de esos megas son el audio horneado) que se carga en un
emulador o en una flashcard. No queda aritmética de coma flotante en el camino
de render, y el framebuffer usa el mismo formato indexado de 8 bits que el modo
4 de la consola.

Jugando va a **12-15 fps** y la pantalla de título a **29,9**, que era el
objetivo del port. Empezó a 2,6.

MVP (sección 29 del documento de diseño):

- [x] DDA
- [x] Renderizado de paredes con textura
- [x] Movimiento y colisiones
- [x] Generación procedural
- [x] Seed reproducible
- [x] Jugador con integridad (HP)
- [x] Un enemigo, el WARDEN
- [x] Un arma, la PACKET GUN
- [x] Disparo hitscan
- [x] Los guardianes reciben daño
- [x] El jugador recibe daño
- [x] Salida y cambio de piso
- [x] Fin de run y run nueva
- [x] HUD, pantallas de bienvenida y reglas, marcador final
- [x] Objetos de sistema: RAM, PATCH y CACHE
- [x] Cámara sellada con llave de archivo
- [x] Jefe final, el NUCLEO CENTINELA
- [x] Música y efectos de sonido
- [x] Ratón, mando y selección de archivo
- [x] Port a Game Boy Advance, jugable a 12-15 fps y con sonido

## Cómo se construyó

Esto no empezó siendo un motor. Empezó siendo un pixel.

### Un pixel

La primera pieza fue el `Framebuffer`: un array de bytes del tamaño de la
pantalla, y una función para pintar uno.

```cpp
void setPixel(int x, int y, uint8_t color);
```

No había nada más. Pero con eso ya se puede construir todo lo demás sin volver a
tocar el hardware: un bucle de `setPixel` es una línea, dos bucles anidados son
un rectángulo, y `fillRect` sale de ahí. Todo lo que dibuja este juego, hasta el
último glifo de la fuente, acaba llamando a esas dos funciones.

### Un mapa visto desde arriba

Con rectángulos ya se puede dibujar un laberinto. `Maze::load` lee un archivo de
texto donde cada carácter es una celda, y pintando un cuadrito por cada celda
que sea pared sale el mapa completo, en 2D y desde arriba. Ese fue el primer
render de verdad: sin perspectiva, sin cámara, un plano.

Ahí también aparecieron el jugador (una posición y un ángulo) y las colisiones,
que a esa altura eran comprobar si la celda a la que quieres entrar es pared.

### Un rayo

El salto interesante es el siguiente. Desde la posición del jugador, y en la
dirección a la que mira, avanzas en pasos pequeños hasta chocar con una pared.
Pintas el camino recorrido y tienes un rayo dibujado sobre el mapa 2D.

Eso, repetido en abanico para cubrir el campo de visión, dibuja lo que el
jugador «ve», todavía en planta. **Esa vista sigue en el juego**: es el minimapa
de la esquina. Los rayos que salen del punto del jugador se calculan con la
misma función `castRay` que alimenta la vista en primera persona, solo que el
minimapa dibuja 24 y no uno por columna, porque para ver hacia dónde miras
sobran.

### De un rayo a una columna

Lo único que hace falta para pasar a 3D es darse cuenta de que **la distancia a
la que un rayo choca es la altura de la pared en pantalla**. Cerca, alto. Lejos,
bajito. La proyección entera es una división:

```cpp
fx stakeHeight = fxMul(fxInt(h), fxRecip(hit.perpDist));
```

Se lanza un rayo por columna de pantalla, se calcula esa altura, y se pinta una
raya vertical centrada. Doscientas cuarenta rayas verticales y eso es la vista
en primera persona. No hay más. (En la consola acabó siendo un rayo cada dos
columnas, por razones que están más abajo.)

Dos detalles que no son obvios y que costaron:

**El ojo de pez.** Si usas la distancia real del rayo, las paredes se curvan
hacia los bordes de la pantalla, porque los rayos de los extremos recorren más
camino hasta la misma pared plana. Hay que quedarse con la distancia
*perpendicular* al plano de la cámara, no con la del rayo. La prueba
`raycaster` existe justo para eso: mira de frente a una pared plana y comprueba
que todas las columnas devuelven la misma distancia.

**Avanzar a pasos pequeños es carísimo y además impreciso.** La forma buena es
DDA: en vez de avanzar un poco y preguntar, calculas de una vez cuánto falta
para cruzar la siguiente línea de la rejilla, saltas justo a ella y compruebas
esa celda. Das un paso por celda cruzada, no cien pasos por celda.

### Y luego texturas

Una columna de color plano ya es un pasillo, pero es un pasillo aburrido. Para
texturarla hace falta saber *en qué punto exacto* de la pared pegó el rayo: eso
da la coordenada horizontal de la textura, y la vertical sale de recorrer la
columna de arriba abajo. Cada pixel de la raya vertical lee un texel.

Las seis texturas de pared y todos los sprites no son archivos de imagen. Los
dibuja código en `src/engine/Textures.h`, y en el build de GBA se hornean a
datos constantes para que vivan en la ROM y no haya que generarlos al arrancar.

El sombreado por distancia también está horneado, pero en la paleta: cada color
base aparece ya multiplicado por seis niveles de luz, así que oscurecer un pixel
es sumarle un entero al índice en vez de escalar tres componentes de color.

## Cómo se hizo rápido

Todo lo anterior corría de sobra en un PC. En la Game Boy Advance, el primer
frame jugable costó **6.460.017 ciclos**. En un treintavo de segundo caben
559.333. O sea 2,6 fps.

Lo que sigue es el camino de ahí hasta aquí, y cada fila está medida en el
emulador antes y después del cambio, nunca estimada:

| Cambio | Ciclos por frame | Equivale a |
| --- | ---: | --- |
| Punto de partida | 6.460.017 | 2,6 fps |
| Volcado a VRAM con DMA3 en vez de la CPU | 5.617.317 | 3,0 fps |
| Texels de pared por puntero en vez de `setPixel` | 4.212.843 | 4,0 fps |
| `WAITCNT` = 0x4317 (esperas del cartucho y prefetch) | 2.527.707 | 6,6 fps |
| DDA, texturas y fuente a IWRAM compilados en ARM | 1.965.916 | 8,5 fps |
| Minimapa reescalado, celdas y rayos sin llamadas ni divisiones | 1.685.017 | 10,0 fps |
| `Framebuffer` a IWRAM | 1.404.127 | 12,0 fps |
| Texto por puntero en vez de un `fillRect` por pixel | 1.404.125 | 12,0 fps |
| Un rayo cada dos columnas en la consola | **1.123.221** | **14,9 fps** |

### La cifra honesta

Esa columna es **un frame**: el segundo después de cargar el primer archivo, con
el jugador parado. Medido sobre 200 frames con el jugador girando y avanzando, y
entrando también por el último archivo (mapa de 44×44 en vez de 28×28), sale
otra cosa:

| Dónde | mín | máx | media |
| --- | ---: | ---: | ---: |
| Archivo 1 | 18,8 fps | 12,0 fps | **13,5 fps** |
| Archivo 5 | 14,9 fps | 10,0 fps | **12,4 fps** |

Lo que crece con el piso es el minimapa (de 90.527 a 161.930 ciclos, porque el
mapa es más grande) y los sprites (de 25.808 a 97.944, porque hay más guardianes
despiertos). Un frame quieto en el primer archivo no ve ninguna de las dos cosas.

### Lo que ordenó todo lo demás

El coste dominante nunca fue el pixel. Fue **buscar en la ROM el código que lo
escribía**.

La ROM de un cartucho es lenta y arranca con cuatro ciclos de espera por acceso.
Como todo el código se ejecuta desde ahí, eso es un impuesto sobre cada
instrucción. Por eso quitar una llamada a función del bucle de texels valió 2,5
veces, y por eso una sola línea configurando las esperas del bus valió 1,67
veces sobre el frame entero:

```cpp
*reinterpret_cast<volatile uint16_t*>(0x4000204) = 0x4317;
```

IWRAM es el último escalón de lo mismo: 32 KB de memoria interna con bus de 32
bits y cero esperas. Mover ahí el código caliente, y compilarlo en ARM en vez de
Thumb ya que cada instrucción de 32 bits se lee de una sola vez, fue el otro
salto grande.

### Los escalones invisibles

`present()` espera al vblank, así que **cada frame cuesta un múltiplo entero de
los 280.896 ciclos que dura un barrido de pantalla**. Un ahorro no se ve en los
fps hasta que cruza uno de esos escalones. El frame jugando bajó de nueve
barridos a cinco, y el de título de tres a dos.

Esto también explica por qué a veces optimizar parece no servir de nada y otras
veces una mejora pequeña dispara el contador.

### Tres veces el sospechoso obvio no era el culpable

Y medir antes de tocar fue lo único que lo evitó.

En el minimapa parecía que la factura estaba en el abanico de 24 rayos. Estaba
en las celdas de muro, que hacían una llamada a `fillRect` por celda.

Mover `Hud.o` a IWRAM valió un 0,9%. Lo caro no era el código del HUD sino el
`Framebuffer` al que llamaba, que seguía en la ROM. Moviendo ese, que ocupa
1 KB en vez de 7, el frame mejoró casi al doble.

Y el HUD no necesitó irse a una capa de hardware, cosa que además es imposible
en modo 4 porque no tiene capas de tiles. El problema real era que `drawText`
pedía un `fillRect` entero por cada pixel encendido de cada glifo.

### Dos cambios que lo empeoraron antes de mejorarlo

Los dos en el bucle de texels, que es el sitio más sensible del motor.

Envolver la escritura en un `for` interno de longitud variable lo llevó de
267.756 a 682.040 ciclos. Y un `std::memcpy` de dos bytes, que en teoría debería
compilar a un `strh`, lo dejó en 557.592 porque con este toolchain no lo hace.
La versión que funciona escribe los dos píxeles como un halfword usando un
`typedef` con `may_alias`.

### Un fallo latente que salió al mover código

Mover `Renderer.o` a IWRAM hizo que el juego se colgara después de la pantalla
de título. La causa no tenía nada que ver con IWRAM: `Game` mide unos 22 KB
(`Nav` son 16.384 bytes de campos de navegación y cola, `Maze` otros 4.096) y
era una variable local de `main`. La pila de la consola son 32 KB, los mismos
que ahora compartía con el código, así que el puntero de pila bajaba por debajo
del código y lo machacaba.

Como `static` el objeto vive en `.bss`, que este linker script manda a EWRAM.
El fallo estaba ahí desde el principio, con unos 10 KB de margen; mover código a
IWRAM solo lo destapó.

### El audio

Cuesta unos 11.000 ciclos por frame de media y no movió el promedio de fps de
forma apreciable: 12,5 antes, 12,4 después. Pero para llegar ahí hubo que pagar
dos cosas.

La primera versión costaba 195.000 ciclos por frame. El mezclador corre en una
interrupción de VBlank, o sea cuatro o cinco veces por cada frame del juego, y
desde la ROM salía más caro que dibujar todos los sprites. Se arregló igual que
todo lo demás de este port, moviendo `Audio.o` a IWRAM compilado en ARM, más
sacar del bucle por muestra las comprobaciones que no cambian dentro de él.

La segunda fue un registro mal escrito. `REG_DMA2CNT_H` estaba puesto en
`0x0CE`, que es la mitad alta de `DMA2DAD` y no el control del canal, así que el
canal de efectos nunca se encendía: el cartucho sonaba con música y sin un solo
disparo. Los registros de DMA van de doce en doce bytes desde `0x0BC`, o sea que
el control del segundo canal está en `0x0D2`.

## Cómo jugar, paso a paso

Hay **dos juegos** en este repositorio, y se compilan por caminos distintos:

- **La versión de escritorio** (`./build/violethat`): ventana de 900×600, ratón
  y mando, la que corre a cientos de FPS. Necesita CMake y SDL2.
- **El cartucho de Game Boy Advance** (`violethat.gba`): 240×160, 12-15 FPS,
  para un emulador o una flashcard. Necesita devkitARM y ffmpeg.

Se puede compilar una sin la otra. Si solo quieres verlo funcionando, la de
escritorio es la ruta corta.

### Ni compilar: descarga la ROM

Si solo quieres jugarlo, no hace falta compilar nada. La ROM ya hecha está en
las Releases:

**[⬇ Descargar violethat.gba (Release v1.0.0)](https://github.com/eldmark/gba_raycaster/releases/latest)**

Ábrela con cualquier emulador de GBA ([mGBA](https://mgba.io/downloads.html)
tiene versión para Windows, macOS y Linux) o cópiala a una flashcard. Los
controles están en «Controles del cartucho», más abajo.

Lo que sigue es para compilarlo desde el código, o para jugar la versión de
escritorio, que no se distribuye compilada.

### La forma rápida: `./install.sh`

En cualquier Linux con apt, dnf o pacman, un solo comando instala todo y compila
las dos versiones:

```sh
git clone https://github.com/eldmark/gba_raycaster.git
cd gba_raycaster
./install.sh
```

Instala las dependencias, baja devkitPro para el cartucho, compila escritorio y
GBA, pasa las pruebas y al terminar dice cómo lanzar cada versión. En Windows se
ejecuta **dentro de WSL**, no en PowerShell (más abajo está cómo instalar WSL).

```sh
./install.sh --no-gba      # solo el build de escritorio, sin devkitPro
./install.sh --skip-deps   # ya tienes todo instalado, solo compila
./install.sh --no-build    # solo instala dependencias
```

Necesita `sudo` para los paquetes, así que te pedirá la contraseña. Si prefieres
ver qué hace antes de darle permisos, es un archivo de 147 líneas y no esconde
nada raro. El resto de esta sección es exactamente lo que el script hace, paso a
paso y a mano.

### Requisitos

| Para | Qué hace falta | Paquete en Debian/Ubuntu |
| --- | --- | --- |
| Escritorio | compilador C++17 | `build-essential` |
| Escritorio | CMake ≥ 3.16 | `cmake` |
| Escritorio | SDL2 (ventana, teclado, mando, audio) | `libsdl2-dev` |
| GBA | devkitARM + libgba | `gba-dev` de devkitPro (ver abajo) |
| GBA | ffmpeg (hornea el audio a PCM) | `ffmpeg` |
| GBA | Python 3 | `python3` |
| Jugar la ROM | un emulador de GBA | `mgba-sdl` o `mgba-qt` |

No hay ninguna dependencia más: ni SDL2\_mixer, ni bibliotecas de mates, ni
gestor de paquetes de C++. El motor no enlaza contra nada.

### Linux (Debian, Ubuntu, Mint…)

**1. Instala lo básico y clona el repositorio.**

```sh
sudo apt update
sudo apt install -y build-essential cmake libsdl2-dev git mgba-sdl
git clone https://github.com/eldmark/gba_raycaster.git
cd gba_raycaster
```

**2. Compila y juega la versión de escritorio.**

```sh
cmake -S . -B build
cmake --build build -j
./build/violethat
```

Ya está. Se abre la ventana en la pantalla de título; **Enter** para pasar, y
otra vez **Enter** para entrar.

> Los WAV de música se cargan desde `public/audio` por una ruta absoluta que se
> fija al ejecutar `cmake -S . -B build`. Si mueves la carpeta del proyecto
> después, vuelve a lanzar ese comando o el juego arrancará mudo.

**3. Repite una partida concreta.** La misma seed reconstruye la run entera,
con su mapa, su salida, sus guardianes y sus objetos. Es como se reproduce un
fallo:

```sh
./build/violethat 583291
```

Sin argumento cada partida usa una seed nueva y la imprime al arrancar.

Si en tu distribución los paquetes se llaman de otra forma: en Fedora son
`gcc-c++ cmake SDL2-devel mgba`, y en Arch `base-devel cmake sdl2-compat mgba-sdl`
(en instalaciones anteriores a 2024 el paquete de SDL2 todavía se llama `sdl2`).

### Una nota para Arch

`./install.sh` funciona, pero con una salvedad: **el toolchain de GBA no se
instala solo**. devkitPro reparte su instalador automático en paquetes `.deb`,
así que en Arch hay que añadir sus repositorios a `/etc/pacman.conf` a mano
siguiendo [su wiki](https://devkitpro.org/wiki/devkitPro_pacman) y terminar con
`pacman -S gba-dev`. El script detecta que estás en Arch, te dice esto mismo y
sigue adelante compilando solo la versión de escritorio, sin romperse.

Y para la mayoría de los casos da igual: **para jugar la ROM no hace falta
compilarla**, está en las [Releases](https://github.com/eldmark/gba_raycaster/releases/latest).
El toolchain solo hace falta si quieres tocar el código de la consola.

### Windows: instalando WSL

En Windows no hay build nativo. La forma soportada es **WSL2**, que es Linux de
verdad corriendo dentro de Windows, con ventanas y sonido incluidos (WSLg). No
hace falta máquina virtual ni arrancar en otro sistema.

**Requisitos de Windows:** Windows 11, o Windows 10 versión 21H2 o superior.
Compruébalo con `winver`.

**1. Instala WSL.** Abre **PowerShell como administrador** (botón derecho en
Inicio → «Terminal (Administrador)») y ejecuta:

```powershell
wsl --install
```

Eso instala WSL2 y Ubuntu de una vez. **Reinicia el equipo** cuando termine. Al
volver se abre sola una ventana de Ubuntu que te pide crear un usuario y una
contraseña; esa contraseña es la de `sudo`, no la de Windows.

Si ya tenías WSL de antes, actualízalo para tener las ventanas gráficas:

```powershell
wsl --update
```

**2. Dentro de Ubuntu, sigue la receta de Linux tal cual.** Abre «Ubuntu» desde
el menú Inicio y pega exactamente los mismos comandos de la sección anterior.

**3. Clona dentro del sistema de archivos de Linux, no en `/mnt/c`.** O sea, en
`~` (tu carpeta personal de Ubuntu). Compilar sobre `/mnt/c/...` funciona pero
va varias veces más lento, porque cada acceso a un archivo cruza la frontera
entre los dos sistemas.

**Lo que sí funciona en WSL:** la ventana, el teclado, el ratón capturado y el
sonido. WSLg los conecta solo, sin servidor X ni configuración.

**Lo que no:** un **mando USB** no llega a WSL. Los dispositivos USB necesitan
`usbipd-win` y compartirlos a mano, y para este juego no compensa, porque el
teclado y el ratón cubren todo. Si tienes mando, la ruta buena es la del
apartado siguiente.

**Si la ventana no aparece**, casi siempre es WSL sin actualizar. `wsl --update`
desde PowerShell y `wsl --shutdown` para reiniciarlo. En un Windows 10 anterior
a 21H2 no hay WSLg y haría falta un servidor X aparte (VcXsrv); actualizar
Windows es menos trabajo.

#### La otra ruta para Windows: compilar la ROM y jugarla fuera

Para el cartucho de GBA hay un atajo mejor. Compila la ROM dentro de WSL,
cópiala a Windows y ábrela con un emulador **nativo de Windows**. Así te saltas
cualquier rareza de gráficos, sonido o mandos de WSL:

```sh
# dentro de Ubuntu, tras compilar el cartucho
cp violethat.gba /mnt/c/Users/TU_USUARIO/Desktop/
```

Y en Windows, abre ese `violethat.gba` con [mGBA](https://mgba.io/downloads.html)
o con VisualBoyAdvance-M. El mando funciona ahí sin más.

### Compilar el cartucho de Game Boy Advance

El build de consola **no usa CMake**: tiene su propio makefile porque el
compilador, el linker script y las banderas son otros.

**1. Instala devkitPro.** Es el toolchain de ARM para GBA y no está en los
repositorios de Debian; se instala con su propio instalador:

```sh
sudo apt install -y ffmpeg python3 wget
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman -S gba-dev      # acepta el grupo entero con Enter
```

Queda todo en `/opt/devkitpro`, que es exactamente donde `Makefile.gba` lo
busca por defecto: **no hace falta exportar ninguna variable de entorno**. Si lo
instalaste en otro sitio, pásaselo al hacer make:

```sh
make -f Makefile.gba DEVKITARM=/otra/ruta/devkitARM LIBGBA=/otra/ruta/libgba
```

**2. Compila.**

```sh
make -f Makefile.gba
```

La primera vez tarda **unos 40 segundos de más**: hornea el audio.
`tools/audio_bake.py` convierte los siete WAV de `public/audio` a PCM de 8 bits
con ffmpeg y escribe 2 MB de arrays C en `tools/audio_baked/`. Eso se genera y
no se versiona, por eso ffmpeg está en la lista de requisitos. `clean` no lo
borra a propósito; para rehornear de cero:

```sh
rm -rf tools/audio_baked && make -f Makefile.gba audio
```

Deja **`violethat.gba`** en la raíz, 2,1 MB.

**3. Juégalo.**

```sh
mgba violethat.gba          # frontend SDL, el del paquete mgba-sdl
mgba-qt violethat.gba       # frontend con menús, del paquete mgba-qt
```

Vale cualquier emulador: mGBA, VBA-M, no$gba. mGBA es el que se usó para medir.

En **hardware real** hace falta una flashcard (EverDrive GBA, EZ-Flash): se
copia el `.gba` a la tarjeta SD y se arranca desde el menú de la tarjeta. La ROM
no usa memoria de guardado, así que no hay `.sav` que preparar.

### Controles del cartucho

D-PAD para andar y girar, **A** para disparar, **SELECT** para pausar, y
**START** para continuar y para empezar una run nueva.

Ojo con las teclas al probarlo en un emulador: ahí no se pulsa **P** ni **C**,
que son del build de escritorio. Se pulsa el botón de GBA, y el emulador decide
qué tecla del teclado es cada botón. Con el mapeo por defecto de mGBA:

| Botón de GBA | Tecla en mGBA | En el juego              |
| ------------ | ------------- | ------------------------ |
| D-PAD        | flechas       | Andar y girar            |
| A            | **X**         | **Disparar**             |
| B            | Z             | sin usar                 |
| SELECT       | **Retroceso** | **Pausar**               |
| START        | **Enter**     | **Reanudar** / continuar |

`mgba-qt` tiene menú para remapear las teclas; el frontend SDL no.

### Si algo falla

| Síntoma | Causa y arreglo |
| --- | --- |
| `Could NOT find SDL2` al hacer cmake | falta `libsdl2-dev` |
| El juego de escritorio arranca mudo | moviste la carpeta tras configurar; repite `cmake -S . -B build` |
| `arm-none-eabi-g++: No such file` | devkitPro no instalado, o está fuera de `/opt/devkitpro` |
| `hace falta ffmpeg en el PATH` | `sudo apt install ffmpeg` |
| El cartucho suena a música pero sin disparos | ROM vieja: recompila, era un bug corregido |
| **P** y **C** no pausan en el emulador | son teclas del build de escritorio; en la ROM es **Retroceso** |
| En WSL no se abre ninguna ventana | `wsl --update` y luego `wsl --shutdown` desde PowerShell |
| El mando no responde en WSL | el USB no pasa a WSL; usa el build nativo de Windows para la ROM |

### Para desarrollar

`make -f Makefile.gba profile` compila una segunda ROM, distinta: pulsa START
sola al arrancar y vuelca el coste en ciclos de cada etapa del frame al log de
depuración de mGBA (`mgba -l 4`). Sirve para medir, no para jugar; el cartucho
normal no lleva nada de eso. `profile-maxfloor` hace lo mismo entrando por el
último archivo, que es el caso caro.

Los dos targets de medición dependen de `clean`, así que hay que compilarlos con
**`-j1`**: en paralelo, `clean` corre a la vez que la compilación y sale un ROM
a medias.

El log va a nivel WARN y no INFO porque mGBA registra **cada** transferencia de
DMA a nivel INFO: con el audio alimentando dos FIFO en cada VBlank, `-l 8` son
miles de líneas por segundo que ahogan la medida y frenan al emulador unas 180
veces.

## Controles

| Teclado          | Ratón / mando                        | Acción                     | GBA   |
| ---------------- | ------------------------------------ | -------------------------- | ----- |
| ↑ ↓ / W S        | Stick izquierdo ↕                    | Avanzar / retroceder       | D-PAD |
| ← → / A D        | **Ratón** · stick izquierdo ↔        | Girar                      | D-PAD |
| Espacio · Ctrl   | Clic izquierdo · botón X             | Disparar                   | A     |
| ← → en el título | D-PAD · stick                        | Elegir archivo de entrada  | D-PAD |
| Enter            | START                                | Continuar / nueva run      | START |
| **P**            | SELECT / BACK                        | Pausar                     | SELECT |
| **C**            | START                                | Reanudar                   | START |
| Esc              | (solo teclado)                       | Salir                      | (no aplica) |

Esa columna de la derecha es el **botón** de GBA, no la tecla que se pulsa en el
emulador: eso lo decide el emulador, y está en «Controles del cartucho», más
arriba.

Pausar y reanudar son **dos teclas distintas** y no una que alterna: a los 12-15
fps de la consola un pulso llega a leerse en dos frames seguidos, y con un solo
botón eso entra y sale de la pausa en el mismo toque. En pausa el mundo se
sigue viendo, sin limpiar la pantalla, para no perder de vista dónde estabas.

El contador de FPS se dibuja arriba a la derecha en las dos plataformas, en la
esquina que dejó libre el panel de estadísticas al bajarse. En GBA promedia
ocho frames de ciclos crudos: uno solo salta entre 12 y 18 según lo
que haya delante y el número sería ilegible de tan inquieto.

El ratón gira con el cursor capturado, así que se puede girar sin tope. El
mando se detecta al arrancar **y al enchufarlo con el juego abierto**. Los
sticks entregan una cantidad, no un sí/no, así que la entrada abstracta lleva
dos campos analógicos (`turn` y `thrust`) que en GBA se quedan en cero y no
cuestan nada.

Hay una diferencia real entre los dos: el ratón entrega un desplazamiento ya
hecho y el stick una velocidad. Por eso el giro del ratón **no** se escala por
el tiempo del frame, porque hacerlo ataría la sensibilidad a los FPS, y el del
stick sí.

**Todo el movimiento vive en el stick izquierdo**, igual que en el D-PAD de la
consola: el eje horizontal gira la cámara y el vertical camina. El avance sale
siempre en la dirección de la cámara, así que girar cambia hacia dónde se anda.
El stick derecho no se lee, y solo el botón X dispara.

## Selección de archivo

La pantalla de título deja elegir por cuál de los cinco archivos entrar, con
las flechas. Las puntas del selector solo se dibujan del lado al que todavía se
puede mover. El marcador cuenta los archivos desde donde arrancó la run, no
desde el uno, así que entrar por el quinto no regala cuatro pisos de puntos.

## La cámara sellada

Casi todos los pisos esconden una cámara que no tiene pasillo abierto: su único
acceso está cifrado y desde fuera se ve como una puerta con bandas de aviso.
La llave del archivo está en otra sala del mismo piso, y no se arrastra al
siguiente. Al acercarse con ella el cifrado cae y dentro esperan tres piezas de
sistema juntas.

El generador coloca la cámara sobre roca virgen y la sella cerrando **todas** las
celdas de su anillo que quedaron abiertas, no solo la primera. Sellar una sola
dejaba la cámara accesible por el hueco de al lado cuando el pasillo corría
pegado al anillo. La prueba `level` lo comprueba en treinta mapas: con la puerta
cerrada la cámara es la única parte inalcanzable del piso, y al abrirla el piso
vuelve a ser conexo entero. Eso es lo que impide que la puerta caiga sobre el
camino principal y parta el mapa en dos.

## El núcleo centinela

El último archivo tiene un jefe plantado delante del protocolo de extracción,
con seis veces la vida de un guardián y una barra propia arriba de la pantalla.
Mientras siga en pie el protocolo no responde, así que la run no se puede
terminar esquivándolo. Va a un cuarto de la velocidad del jugador y pega a 1.8
celdas: retroceder disparando es la forma prevista de ganarle, y así es como lo
hace el piloto automático de las pruebas.

## Sonido

Siete pistas WAV en `public/audio`: tema principal para las pantallas, tema de
asalto durante la partida, y efectos de disparo, daño, recogida, cambio de
archivo y victoria. El mezclador está escrito sobre el SDL2 que ya usa la
ventana, sin SDL2\_mixer: los siete archivos son mono de 16 kHz y 16 bits,
exactamente el formato con el que se abre el dispositivo, así que no hay ninguna
conversión que hacer y una dependencia más no compraría nada.

La capa de juego no sabe que existe el sonido. El bucle de escritorio detecta
los flancos de lo que `Game` ya expone (fogonazo, interferencia de daño, aviso
de recogida y estado) y dispara los efectos desde ahí. Por eso `game` y `engine`
siguen compilando tal cual para GBA.

**En consola suena lo mismo**, por DirectSound: `src/gba/Audio.cpp` tiene la
misma interfaz que la versión de SDL (`play`, `setTrack`) y el bucle de GBA hace
exactamente las mismas llamadas, en el mismo orden. La FIFO A lleva la música y
la FIFO B la mezcla de hasta cuatro efectos; sumar las dos lo hace el propio
chip, así que no hay una pasada de mezcla más en software.

Todo se hornea a **10512 Hz**, una sola tasa para música y efectos, y la razón
es la cadencia: un cuadro de vídeo son 280.896 ciclos y 280896/1596 = **176
exacto**. En cada VBlank se consumen 176 muestras justas, ni una más, así que el
DMA se puede rearmar cada VBlank sin acumular deriva. A los 16384 Hz que
proponía el diseño original salen 274,3125 muestras por cuadro y esa fracción
hay que ir arrastrándola. Como además el asset ya está a la tasa de hardware, no
queda ningún remuestreador en tiempo real: reproducir es copiar bytes.

El DMA se rearma en cada VBlank en vez de dejarlo correr porque el hardware no
envuelve el puntero de origen solo: con *Repeat* recarga la cuenta pero sigue
leyendo hacia adelante, así que un canal armado una vez se sale del buffer a los
pocos cuadros. Rearmarlo es además lo que mantiene el sonido enganchado al vídeo
pase lo que pase con la duración del frame de juego: el motor va a 12-15 fps,
pero ese ISR corre a 60 Hz.

El contador de ciclos de `src/gba/Debug.h` tuvo que mudarse a TM2+TM3: el reloj
de muestreo de DirectSound solo puede colgar de TM0 o TM1, lo elige un bit de
`SOUNDCNT_H` y no hay más opciones, mientras que el contador puede vivir en
cualquiera.

## Puntuación

100 puntos por baja y 250 por cada archivo superado, más 750 por abatir al
núcleo centinela y 1000 de prima por salir entero. Se calcula a partir del estado en lugar de acumularse, así no puede
desincronizarse con lo que muestra el HUD.

## Cómo funciona

Cinco pisos. Cada uno se genera con habitaciones y pasillos a partir de la
seed, coloca una salida y reparte guardianes lejos del punto de entrada. Pisar
la salida enlaza con el siguiente archivo y recupera parte de la integridad.
Llegar al final de los cinco cierra la run; quedarse sin integridad también,
pero peor.

Cada sala repartida por el piso esconde una pieza de sistema: RAM sube la
integridad máxima, PATCH cura y CACHE sube el daño del arma. Los guardianes
pegan fuerte, diez de daño en el primer archivo y veintiséis en el quinto, así
que recoger es lo que separa llegar al final de morir en el tercero.

Los guardianes duermen hasta que te ven de verdad, con alcance _y_ línea de
visión, no solo cercanía. A partir de ahí persiguen y golpean con una cadencia
fija. Con el jugador a
la vista van derechos; sin verlo siguen un campo de flujo calculado con una
búsqueda en anchura desde la celda del jugador, compartido por todos y rehecho
cuatro veces por segundo. Sin él se quedaban empujando la esquina que tuvieran
delante, para siempre.

El disparo es hitscan: lanza un rayo desde la cámara y, si hay pared antes que
el guardián, se pierde contra la pared.

## Arquitectura

La separación entre motor y plataforma es la restricción que manda, porque es
lo que hace posible el port (sección 7 del documento de diseño).

```
src/engine/    matemática, mapa, DDA, jugador, renderizador. Sin SDL.
src/game/      generación de niveles, guardianes, reglas de la run. Sin SDL.
src/desktop/   la única capa que conoce SDL2. En GBA se sustituye entera.
src/tests/
```

La biblioteca del motor no enlaza contra SDL. Si alguna vez lo necesitara, la
separación estaría rota y el port dejaría de ser posible.

Todo el camino de render usa punto fijo 16.16 con tabla de senos. El
ARM7TDMI de la Game Boy Advance no tiene unidad de coma flotante, así que cada
`float` pasaría por una biblioteca de software. Los `float` que quedan solo se
ejecutan una vez al arrancar, construyendo la paleta y la tabla.

El sombreado por distancia está horneado en la paleta: cada color base aparece
ya multiplicado por seis niveles de luz, de modo que el bucle interno suma un
entero en lugar de escalar componentes de color. Son seis y no ocho porque con
los seis materiales de pared actuales la rampa a ocho niveles se pasaba de los
256 índices que admite el modo 4 de la consola.

La estética está fijada en `docs/DESIGN.md` (fuera del repositorio). El verde es exclusivo de los
enemigos: es la única señal de peligro del juego y pierde su valor en cuanto
aparece en una pared. Por eso la barra de integridad es rosa y no verde.

El logotipo es arte ASCII de verdad, no un mapa de bits que lo imite: el juego
va de estar dentro de una máquina, y un sombrero dibujado con guiones bajos y
barras dice eso sin explicarlo. Se dibuja con separación cero entre caracteres,
o los trazos salen punteados.

El texto usa una fuente de mapa de bits 5x7 generada desde arte ASCII. Las
pantallas se maquetan midiendo el bloque y centrándolo, no dejándolo fluir hacia
abajo, para que quepan igual en una ventana grande que en los 240x160 de la
consola.

## Pruebas

```sh
ctest --test-dir build --output-on-failure
```

Son siete suites. Cuatro hacen de red de seguridad (enemigos, pantallas, frames
de referencia y equivalencia de rampas de color). Las tres que de verdad
sostienen el juego son estas:

- **`game`** conduce al jugador por los cinco pisos con la misma `Input` que
  produce la capa de plataforma, siguiendo una ruta calculada con BFS y
  disparando a lo que se cruza. Comprueba que la run se puede _terminar_, no
  solo perder despacio.
- **`level`** hace una inundación desde el punto de entrada en treinta mapas
  distintos y comprueba que la cámara sellada es lo único inalcanzable, y solo
  mientras la puerta esté cifrada. Es lo que garantiza que ningún piso se pueda
  generar partido en dos.
- **`raycaster`** verifica que no hay ojo de pez: mirando de frente a una pared
  plana, todas las columnas deben devolver la misma distancia perpendicular. Es
  el único chequeo que detecta una corrección de coseno que falte o que sobre.
