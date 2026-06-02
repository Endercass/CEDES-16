#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>

const uint8_t BIT_UP     = 1 << 0;
const uint8_t BIT_DOWN   = 1 << 1;
const uint8_t BIT_LEFT   = 1 << 2;
const uint8_t BIT_RIGHT  = 1 << 3;
const uint8_t BIT_A      = 1 << 4;
const uint8_t BIT_B      = 1 << 5;
const uint8_t BIT_START  = 1 << 6;
const uint8_t BIT_SELECT = 1 << 7;

struct InputHandle {
    SDL_Rect *input_pad;
    SDL_Rect *btn_up;
    SDL_Rect *btn_down;
    SDL_Rect *btn_left;
    SDL_Rect *btn_right;
    SDL_Rect *btn_a;
    SDL_Rect *btn_b;
    SDL_Rect *btn_start;
    SDL_Rect *btn_select;
};

void pollInput(union Memory *, struct InputHandle *);

void addController(Sint32);
void removeController(Sint32); 