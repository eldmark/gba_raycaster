CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
# recursiva a proposito: solo se evalua al construir `raycaster`, asi `make test`
# no falla por pkg-config cuando SDL2 no esta instalado.
SDL       = $(shell pkg-config --cflags --libs sdl2)

ENGINE   := src/Maze.cpp src/Player.cpp src/Raycaster.cpp src/Renderer.cpp src/Framebuffer.cpp
SRC      := $(ENGINE) src/Platform.cpp src/main.cpp

raycaster: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(SDL) -lm

# El test no toca SDL: prueba el motor puro.
test_raycaster: src/test_raycaster.cpp src/Raycaster.cpp src/Maze.cpp src/Player.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm

test: test_raycaster
	./test_raycaster

clean:
	rm -f raycaster test_raycaster

.PHONY: test clean
