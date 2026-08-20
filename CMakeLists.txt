cmake_minimum_required(VERSION 3.16)
project(violet_hat CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra)

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
target_include_directories(engine PUBLIC src/engine)

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
)
target_link_libraries(violethat PRIVATE game SDL2::SDL2)

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
