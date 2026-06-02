#include <SDL2/SDL.h>
#include <stdint.h>
#include "toyconsole.h"
#include "input.h"

int16_t DEADZONE = 8000;
SDL_GameController* controller = NULL;
SDL_Point mousePos;

void pollInput(union Memory *memory, struct InputHandle * inp) {
    uint8_t in = 0;

    const uint8_t* keyboard = SDL_GetKeyboardState(NULL);
    if (keyboard[SDL_SCANCODE_W] || keyboard[SDL_SCANCODE_UP])          in |= BIT_UP;
    if (keyboard[SDL_SCANCODE_S] || keyboard[SDL_SCANCODE_DOWN])        in |= BIT_DOWN;
    if (keyboard[SDL_SCANCODE_A] || keyboard[SDL_SCANCODE_LEFT])        in |= BIT_LEFT;
    if (keyboard[SDL_SCANCODE_D] || keyboard[SDL_SCANCODE_RIGHT])       in |= BIT_RIGHT;
    if (keyboard[SDL_SCANCODE_Z] || keyboard[SDL_SCANCODE_K])           in |= BIT_A;
    if (keyboard[SDL_SCANCODE_X] || keyboard[SDL_SCANCODE_J])           in |= BIT_B;
    if (keyboard[SDL_SCANCODE_RETURN])                                  in |= BIT_START;
    if (keyboard[SDL_SCANCODE_ESCAPE] || keyboard[SDL_SCANCODE_RSHIFT]) in |= BIT_SELECT;

    if (controller) {
        if (SDL_GameControllerGetAttached(controller)) {
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP))    in |= BIT_UP;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  in |= BIT_DOWN;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  in |= BIT_LEFT;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) in |= BIT_RIGHT;

            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A))          in |= BIT_A;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B))          in |= BIT_B;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START))      in |= BIT_START;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK))       in |= BIT_SELECT;

            int16_t stickX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t stickY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);

            if (stickX < -DEADZONE) in |= BIT_LEFT;
            if (stickX > DEADZONE)  in |= BIT_RIGHT;
            if (stickY < -DEADZONE) in |= BIT_UP;
            if (stickY > DEADZONE)  in |= BIT_DOWN;
        } else {
            controller = NULL;
        }
    }

    uint32_t mouseState = SDL_GetMouseState(&mousePos.x, &mousePos.y);
    if (mouseState & SDL_BUTTON(SDL_BUTTON_LEFT)) {
        if (SDL_PointInRect(&mousePos, inp->btn_up))     in |= BIT_UP;
        if (SDL_PointInRect(&mousePos, inp->btn_down))   in |= BIT_DOWN;
        if (SDL_PointInRect(&mousePos, inp->btn_left))   in |= BIT_LEFT;
        if (SDL_PointInRect(&mousePos, inp->btn_right))  in |= BIT_RIGHT;
 
        if (SDL_PointInRect(&mousePos, inp->btn_a))      in |= BIT_A;
        if (SDL_PointInRect(&mousePos, inp->btn_b))      in |= BIT_B;
        if (SDL_PointInRect(&mousePos, inp->btn_start))  in |= BIT_START;
        if (SDL_PointInRect(&mousePos, inp->btn_select)) in |= BIT_SELECT;
    }


    memory->registers.in = in;
}

void addController(Sint32 joystick_index) {
    if (!controller)
        controller = SDL_GameControllerOpen(joystick_index);
}

void removeController(Sint32 joystick_index) {
    if (controller) {
        SDL_Joystick* joy = SDL_GameControllerGetJoystick(controller);
        if (SDL_JoystickInstanceID(joy) == joystick_index) {
            SDL_GameControllerClose(controller);
            controller = NULL;
        }
    }
}