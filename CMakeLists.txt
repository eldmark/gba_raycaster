cmake_minimum_required(VERSION 3.16)
project(violet_hat CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra)

# --- assets horneados en tiempo de compilacion (Fase 2 del port) -------------
# bake_assets es una herramienta de HOST: corre los mismos generadores de
# Textures.h (fillVault, fillDoor, fillKey, ...) y vuelca el Assets resultante
# como datos `const` en un header generado. En GBA `const` cae en ROM, no en
# .bss, y el arranque no gasta ciclos rellenando texturas.
# Depende del propio Textures.h: si cambia un generador o la paleta, el
# comando se re-ejecuta solo, asi que el header horneado no puede
# desincronizarse del codigo que lo produce.
add_executable(bake_assets tools/bake_assets.cpp)
target_include_directories(bake_assets PRIVATE src/engine)

set(BAKED_ASSETS_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(BAKED_ASSETS_HEADER ${BAKED_ASSETS_DIR}/AssetsData.h)
add_custom_command(
    OUTPUT ${BAKED_ASSETS_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BAKED_ASSETS_DIR}
    COMMAND bake_assets ${BAKED_ASSETS_HEADER}
    DEPENDS bake_assets ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/Textures.h
    COMMENT "Horneando texturas y paleta a ROM (bake_assets)"
)
add_custom_target(baked_assets DEPENDS ${BAKED_ASSETS_HEADER})

# --- motor: portable, sin SDL ni nada de escritorio ---------------------------
# Esta biblioteca es la que sobrevive intacta al port a Game Boy Advance.
# Si alguna vez necesita enlazar contra SDL, algo se rompio.
add_library(engine STATIC
    src/engine/Fixed.cpp
    src/engine/Maze.cpp
    src/engine/Player.cpp
    src/engine/Raycaster.cpp
    src/engine/Renderer.cpp
    src/engine/Framebuffer.cpp
    src/engine/Text.cpp
)
target_include_directories(engine PUBLIC src/engine ${BAKED_ASSETS_DIR})
add_dependencies(engine baked_assets)

# --- juego: mundo, entidades y reglas ----------------------------------------
# Tampoco depende de la plataforma. Separada del motor porque el motor no debe
# saber que existen enemigos ni pisos (seccion 7 del PROJECT.md).
add_library(game STATIC
    src/game/Level.cpp
    src/game/Game.cpp
    src/game/Enemy.cpp
    src/game/Hud.cpp
    src/game/Nav.cpp
)
target_include_directories(game PUBLIC src/game)
target_link_libraries(game PUBLIC engine)

# --- plataforma + ejecutable de escritorio ------------------------------------
find_package(SDL2 REQUIRED)

add_executable(violethat
    src/desktop/main.cpp
    src/desktop/Platform.cpp
    src/desktop/Audio.cpp
)
target_link_libraries(violethat PRIVATE game SDL2::SDL2)

# Los WAV se leen desde el repositorio en vez de copiarse al directorio de
# build: son casi 6 MB y duplicarlos en cada configuracion no aporta nada. En
# GBA el sonido va empaquetado en la ROM y esta ruta no existe.
target_compile_definitions(violethat PRIVATE
    AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public/audio")

# el ejecutable busca maze.txt en el directorio de trabajo
configure_file(maze.txt ${CMAKE_CURRENT_BINARY_DIR}/maze.txt COPYONLY)

# --- tests --------------------------------------------------------------------
# Solo enlazan contra engine: si el test necesitara SDL, la separacion de capas
# estaria mal hecha.
enable_testing()
# el build Release define NDEBUG, que borraria todos los assert y dejaria los
# tests pasando siempre. Se anula solo para estos targets.
foreach(suite raycaster level game enemy screens)
    add_executable(test_${suite} src/tests/test_${suite}.cpp)
    target_link_libraries(test_${suite} PRIVATE game)
    target_compile_options(test_${suite} PRIVATE -UNDEBUG)
    add_test(NAME ${suite}_unit
             COMMAND test_${suite}
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# Prueba de equivalencia RGB del dedup de rampas de pared (ver el comentario
# en tools/verify_wall_dedup.cpp): no es un test dorado de framebuffer, es la
# prueba de que deduplicar indices no cambio ni un canal de color.
add_executable(verify_wall_dedup tools/verify_wall_dedup.cpp)
target_link_libraries(verify_wall_dedup PRIVATE engine)
target_compile_options(verify_wall_dedup PRIVATE -UNDEBUG)
add_test(NAME wall_dedup_rgb_equivalence
         COMMAND verify_wall_dedup
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
