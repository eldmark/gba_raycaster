#include "Platform.h"

#include "Framebuffer.h"
#include "Textures.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <vector>

// SDL solo se incluye aqui: los headers del motor no deben arrastrarlo,
// por eso los handles viven como void* en Platform.h.

bool Platform::init(int w, int h, const char* title) {
    width_ = w;
    height_ = h;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    sdlReady_ = true;

    SDL_Window* window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, w, h, 0);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    window_ = window;

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    renderer_ = renderer;

    // una sola textura para toda la vida del programa; el original en Rust
    // creaba una por frame.
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!texture) {
        std::fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return false;
    }
    texture_ = texture;

    return true;
}

bool Platform::pollInput(Input& input) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
    }

    // estado del teclado, no eventos: el movimiento debe ser continuo
    // mientras la tecla siga pulsada.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_ESCAPE]) return false;

    input.left = keys[SDL_SCANCODE_LEFT];
    input.right = keys[SDL_SCANCODE_RIGHT];
    input.fwd = keys[SDL_SCANCODE_UP];
    input.back = keys[SDL_SCANCODE_DOWN];
    input.start = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE];
    return true;
}

void Platform::present(const Framebuffer& fb) {
    SDL_Texture* texture = static_cast<SDL_Texture*>(texture_);
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(renderer_);

    // el framebuffer es indexado; aca se expande a ARGB con la paleta. En GBA
    // este paso no existe: el hardware lee la paleta por su cuenta.
    // ponytail: lookup lineal en CPU, suficiente a 900x600.
    static std::vector<uint32_t> argb;
    const size_t n = size_t(fb.width()) * size_t(fb.height());
    argb.resize(n);
    const uint8_t* src = fb.pixels();
    const uint32_t* pal = palette();
    for (size_t i = 0; i < n; ++i) argb[i] = pal[src[i]];

    SDL_UpdateTexture(texture, nullptr, argb.data(),
                      fb.width() * int(sizeof(uint32_t)));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

unsigned long long Platform::ticksMs() const {
    return SDL_GetTicks64();
}

void Platform::setTitle(const char* title) {
    SDL_SetWindowTitle(static_cast<SDL_Window*>(window_), title);
}

void Platform::shutdown() {
    // se anula todo para que un segundo shutdown no haga nada.
    if (texture_) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(texture_));
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
    }
    // solo si el init llego a correr: main llama a shutdown() tambien cuando
    // init() fallo, y un SDL_Quit sin SDL_Init es comportamiento indefinido.
    if (sdlReady_) {
        SDL_Quit();
        sdlReady_ = false;
    }
}
