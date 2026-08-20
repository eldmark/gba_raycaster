cmake_minimum_required(VERSION 3.16)
project(gba_raycaster CXX)

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
)
target_include_directories(engine PUBLIC src/engine)

# --- plataforma + ejecutable de escritorio ------------------------------------
find_package(SDL2 REQUIRED)

add_executable(raycaster
    src/desktop/main.cpp
    src/desktop/Platform.cpp
)
target_link_libraries(raycaster PRIVATE engine SDL2::SDL2)

# el ejecutable busca maze.txt en el directorio de trabajo
configure_file(maze.txt ${CMAKE_CURRENT_BINARY_DIR}/maze.txt COPYONLY)

# --- tests --------------------------------------------------------------------
# Solo enlazan contra engine: si el test necesitara SDL, la separacion de capas
# estaria mal hecha.
enable_testing()
add_executable(test_raycaster src/tests/test_raycaster.cpp)
target_link_libraries(test_raycaster PRIVATE engine)
# el build Release define NDEBUG, que borraria todos los assert y dejaria el
# test pasando siempre. Se anula solo para este target.
target_compile_options(test_raycaster PRIVATE -UNDEBUG)
add_test(NAME raycaster_unit
         COMMAND test_raycaster
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
