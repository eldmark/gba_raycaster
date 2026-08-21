#include "Platform.h"

#include "Framebuffer.h"
#include "Textures.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <vector>

// SDL solo se incluye aqui: los headers del motor no deben arrastrarlo,
// por eso los handles viven como void* en Platform.h.

namespace {

// Unidades de angulo por pixel de raton. 65536 son una vuelta, asi que con 40
// hacen falta unos 1600 px de recorrido para girar 360 grados.
constexpr int MOUSE_SENS = 40;

// Giro maximo del stick, en unidades de angulo por segundo. Es el mismo valor
// que usa el teclado en Player.cpp, de modo que a fondo el stick gira igual de
// rapido que mantener una flecha.
constexpr float STICK_TURN_PER_SEC = 31294.0f;

// Inclinacion maxima del stick, en fracciones de media pantalla por segundo.
// Con 1.2 el recorrido util entero (Player::MAX_PITCH, 0.6 a cada lado) se
// cubre en medio segundo: rapido para apuntar, lento para no marear.
constexpr float STICK_LOOK_PER_SEC = 1.2f;

// Unidades de pitch por pixel de raton, en fracciones de media pantalla. El
// recorrido util son ~360 px, parecido a los 1600 px de la vuelta horizontal.
constexpr float MOUSE_LOOK_SENS = 1.0f / 300.0f;

// Zona muerta del stick analogico sobre 32767. Sin ella el jugador gira solo:
// ningun stick real descansa exactamente en cero.
constexpr int DEADZONE = 8000;

// Pasa un eje de -32768..32767 a -1..1 aplicando la zona muerta y reescalando
// lo que queda, para que el primer milimetro util no sea ya un salto.
float axis(int value) {
    if (value > -DEADZONE && value < DEADZONE) return 0.0f;
    const float sign = value < 0 ? -1.0f : 1.0f;
    float mag = (float(value < 0 ? -value : value) - DEADZONE) / (32767.0f - DEADZONE);
    if (mag > 1.0f) mag = 1.0f;
    return sign * mag;
}

}  // namespace

bool Platform::init(int w, int h, const char* title) {
    width_ = w;
    height_ = h;

    // El mando entra en el mismo init: si falta el subsistema, SDL no reporta
    // nunca los eventos de conexion y el mando no aparece ni al enchufarlo.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
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

    // Raton capturado: en modo relativo el cursor no toca los bordes de la
    // ventana, asi que se puede girar sin limite. Se suelta solo al salir.
    SDL_SetRelativeMouseMode(SDL_TRUE);

    // Mando ya conectado al arrancar. Los que se enchufen despues llegan como
    // evento en pollInput.
    for (int i = 0; i < SDL_NumJoysticks() && !pad_; ++i) {
        if (SDL_IsGameController(i)) pad_ = SDL_GameControllerOpen(i);
    }

    lastPoll_ = SDL_GetTicks64();
    return true;
}

bool Platform::pollInput(Input& input) {
    input = Input{};

    // El movimiento del raton se ACUMULA por eventos, no se lee de un estado:
    // entre dos frames puede haber varios eventos de movimiento y quedarse con
    // el ultimo perderia parte del giro.
    int mouseDX = 0, mouseDY = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                return false;
            case SDL_MOUSEMOTION:
                mouseDX += ev.motion.xrel;
                mouseDY += ev.motion.yrel;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad_) pad_ = SDL_GameControllerOpen(ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad_ && ev.cdevice.which ==
                                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(
                                    static_cast<SDL_GameController*>(pad_)))) {
                    SDL_GameControllerClose(static_cast<SDL_GameController*>(pad_));
                    pad_ = nullptr;
                }
                break;
            default:
                break;
        }
    }

    const unsigned long long now = SDL_GetTicks64();
    const float frameSeconds = float(now - lastPoll_) / 1000.0f;
    lastPoll_ = now;

    // estado del teclado, no eventos: el movimiento debe ser continuo
    // mientras la tecla siga pulsada.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_ESCAPE]) return false;

    input.left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
    input.right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
    input.fwd = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
    input.back = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
    input.fire = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_LCTRL] ||
                 (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT));
    input.start = keys[SDL_SCANCODE_RETURN];

    input.turn = mouseDX * MOUSE_SENS;
    // Hacia arriba el raton da yrel negativo, y mirar arriba baja el horizonte:
    // el signo se invierte una sola vez, aqui.
    input.look = fx(float(-mouseDY) * MOUSE_LOOK_SENS * float(FX_ONE));

    if (pad_) {
        SDL_GameController* pad = static_cast<SDL_GameController*>(pad_);
        auto held = [&](SDL_GameControllerButton b) {
            return SDL_GameControllerGetButton(pad, b) != 0;
        };

        // El D-PAD se suma a las flechas; los menus se manejan con el mismo
        // left/right por flanco que el teclado.
        input.left = input.left || held(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        input.right = input.right || held(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        input.fwd = input.fwd || held(SDL_CONTROLLER_BUTTON_DPAD_UP);
        input.back = input.back || held(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        input.fire = input.fire || held(SDL_CONTROLLER_BUTTON_A) ||
                     SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >
                         DEADZONE;
        input.start = input.start || held(SDL_CONTROLLER_BUTTON_START);

        // Stick izquierdo: apuntar. Stick derecho: moverse. Los dos ejes de la
        // vista viven en el mismo pulgar a proposito; antes el eje X del stick
        // de movimiento tambien giraba la camara, asi que avanzar en diagonal
        // hacia adelante rotaba la vista sin haberlo pedido, y eso es lo que
        // marea. Un stick mueve, el otro mira, y ninguno hace las dos cosas.
        //
        // A diferencia del raton esto es una velocidad, asi que va multiplicado
        // por el tiempo del frame.
        const float turn = axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
        input.turn += int32_t(turn * STICK_TURN_PER_SEC * frameSeconds);

        // El eje Y de SDL crece hacia abajo, asi que empujar el stick hacia
        // adelante da negativo y hay que invertirlo en los dos casos: arriba es
        // mirar arriba, y arriba es avanzar.
        const float look =
            -axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
        input.look += fx(look * STICK_LOOK_PER_SEC * frameSeconds * float(FX_ONE));

        const float thrust =
            -axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));
        input.thrust = fx(thrust * float(FX_ONE));
    }

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
    if (pad_) {
        SDL_GameControllerClose(static_cast<SDL_GameController*>(pad_));
        pad_ = nullptr;
    }
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
