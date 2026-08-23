#!/usr/bin/env bash
#
# Instala las dependencias de VIOLET HAT y compila el juego. Pensado para
# ejecutarse una sola vez en una maquina limpia:
#
#     ./install.sh
#
# Deja listas las dos versiones: ./build/violethat (escritorio) y
# violethat.gba (cartucho de Game Boy Advance).
#
# Opciones:
#     --no-gba      solo el build de escritorio; no instala devkitPro
#     --no-build    solo instala dependencias, no compila
#     --skip-deps   no instala nada, solo compila (ya tienes todo)
#     -h, --help
#
# En Windows esto se corre DENTRO de WSL, no en PowerShell. Ver el README.

set -euo pipefail

WANT_GBA=1
WANT_BUILD=1
WANT_DEPS=1

for arg in "$@"; do
    case "$arg" in
        --no-gba)   WANT_GBA=0 ;;
        --no-build) WANT_BUILD=0 ;;
        --skip-deps) WANT_DEPS=0 ;;
        -h|--help)  sed -n '3,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opcion desconocida: $arg (usa --help)" >&2; exit 2 ;;
    esac
done

cd "$(dirname "$(readlink -f "$0")")"

# Colores solo si la salida es una terminal: redirigido a un archivo, los
# codigos de escape solo estorban.
if [ -t 1 ]; then B=$'\033[1m'; G=$'\033[32m'; Y=$'\033[33m'; R=$'\033[31m'; N=$'\033[0m'
else B=""; G=""; Y=""; R=""; N=""; fi
step() { printf '\n%s==>%s %s%s\n' "$G" "$N" "$B" "$1$N"; }
warn() { printf '%s!!%s %s\n' "$Y" "$N" "$1"; }
die()  { printf '%sxx%s %s\n' "$R" "$N" "$1" >&2; exit 1; }

# sudo solo si no somos ya root: dentro de un contenedor no suele estar
# instalado y pedirlo fallaria por nada.
SUDO=""
if [ "$WANT_DEPS" -eq 1 ] && [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || die "hace falta sudo, o correr esto como root"
    SUDO="sudo"
fi

# --- que gestor de paquetes hay -----------------------------------------------
PM=none
if [ "$WANT_DEPS" -eq 0 ]; then
    step "Salto la instalacion de dependencias (--skip-deps)"
elif command -v apt-get >/dev/null 2>&1; then PM=apt
elif command -v dnf     >/dev/null 2>&1; then PM=dnf
elif command -v pacman  >/dev/null 2>&1; then PM=pacman
else
    die "no reconozco el gestor de paquetes. Instala a mano: compilador C++17, cmake, SDL2 (dev), ffmpeg, python3, y un emulador de GBA."
fi

[ "$PM" != none ] && step "Instalando dependencias del build de escritorio ($PM)"
case "$PM" in
    none) ;;
    apt)
        $SUDO apt-get update
        $SUDO apt-get install -y build-essential cmake libsdl2-dev git ffmpeg python3 wget mgba-sdl
        ;;
    dnf)
        $SUDO dnf install -y gcc-c++ cmake SDL2-devel git ffmpeg python3 wget mgba
        ;;
    pacman)
        $SUDO pacman -S --needed --noconfirm base-devel cmake sdl2 git ffmpeg python wget mgba-sdl
        ;;
esac

# --- devkitPro, solo para el cartucho -----------------------------------------
# No esta en los repositorios de ninguna distribucion: trae su propio
# instalador, que a su vez instala un pacman aparte (dkp-pacman) en
# /opt/devkitpro. Ese prefijo es justo donde Makefile.gba busca por defecto,
# asi que no hay ninguna variable de entorno que exportar.
if [ "$WANT_GBA" -eq 1 ]; then
    if [ -x /opt/devkitpro/devkitARM/bin/arm-none-eabi-g++ ]; then
        step "devkitARM ya esta instalado, lo salto"
    elif [ "$PM" = none ]; then
        warn "devkitARM no esta y --skip-deps me dice que no instale nada."
        warn "Instalalo o usa --no-gba."
        WANT_GBA=0
    elif [ "$PM" != apt ]; then
        warn "El instalador de devkitPro solo trae paquetes .deb, y esta maquina no usa apt."
        warn "Instalalo a mano siguiendo https://devkitpro.org/wiki/Getting_Started y vuelve a"
        warn "correr esto, o usa --no-gba para quedarte solo con el build de escritorio."
        WANT_GBA=0
    else
        step "Instalando devkitPro (toolchain de Game Boy Advance)"
        tmp="$(mktemp -d)"
        trap 'rm -rf "$tmp"' EXIT
        wget -q --show-progress -O "$tmp/install-devkitpro-pacman" \
            https://apt.devkitpro.org/install-devkitpro-pacman
        chmod +x "$tmp/install-devkitpro-pacman"
        $SUDO "$tmp/install-devkitpro-pacman"
        # --noconfirm sobre el grupo entero: gba-dev son compilador, libgba y
        # las herramientas, y no tiene sentido elegir un subconjunto.
        $SUDO /opt/devkitpro/tools/bin/dkp-pacman -S --needed --noconfirm gba-dev
    fi
fi

if [ "$WANT_BUILD" -eq 0 ]; then
    step "Dependencias listas. Compila cuando quieras con:"
    echo "    cmake -S . -B build && cmake --build build -j"
    [ "$WANT_GBA" -eq 1 ] && echo "    make -f Makefile.gba"
    exit 0
fi

# --- compilar -----------------------------------------------------------------
step "Compilando la version de escritorio"
cmake -S . -B build
cmake --build build -j"$(nproc)"

step "Pasando las pruebas"
( cd build && ctest --output-on-failure )

if [ "$WANT_GBA" -eq 1 ]; then
    # La primera vez esto hornea 2 MB de PCM con ffmpeg y tarda ~40 s de mas.
    # -j1 a proposito: no vale la pena paralelizar y evita sorpresas con los
    # targets que dependen de clean.
    step "Compilando el cartucho de Game Boy Advance (hornea el audio la primera vez)"
    make -f Makefile.gba -j1
fi

step "Listo"
cat <<EOF

  Escritorio:   ./build/violethat
                ./build/violethat 583291    (repite esa run exacta)
EOF
if [ "$WANT_GBA" -eq 1 ]; then
cat <<EOF
  Cartucho:     mgba violethat.gba

  Teclas en mGBA: flechas para andar, X dispara,
                  Retroceso pausa, Enter continua.
EOF
fi
echo
