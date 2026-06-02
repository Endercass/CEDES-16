#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define MEM_SIZE      (1 << 16)
#define DSP_WIDTH (1 << 7)
#define DSP_HEIGHT (1 << 7)
#define DSP_FB_SIZE       (DSP_WIDTH * DSP_HEIGHT)
#define AUD_SAMPLE_RATE   22050
#define VOICE_COUNT   8
#define WAVE_SIZE     64

#define REG_PC           0x00 // Program Counter (16-bit)
#define REG_SP           0x02 // Stack Pointer (16-bit)
#define REG_DP           0x04 // Display Pointer (16-bit)
#define REG_AP           0x06 // Audio Pointer (16-bit)
#define REG_IN           0x08 // Input Register (8-bit)
#define REG_FL           0x09 // Flags Register (8-bit)
#define REG_IM           0x0A // Immediate Register (16-bit)

#define FLAG_CF          (1 << 1) // Carry Flag
#define FLAG_OF          (1 << 3) // Overflow Flag
#define FLAG_GF          (1 << 4) // Grow Flag (0 = down, 1 = up)
#define FLAG_HF          (1 << 5) // Halt Flag
#define FLAG_VF          (1 << 6) // Virtual Immediate Flag

struct Registers {
    // Program counter
    uint16_t pc;
    // Stack pointer
    uint16_t sp;
    // Display pointer
    uint16_t dp;
    // Audio pointer
    uint16_t ap;
    // Input register
    uint8_t in;
    // Flags register
    uint8_t fl;
    // Immediate value register
    uint16_t im;
};

union Immediate {
    uint8_t imm8;
    uint16_t imm16;
};

union Memory {
    uint8_t bytes[MEM_SIZE];
    struct Registers registers;
};

struct GfxHandle {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Rect screen;
    SDL_Rect bezel;
    SDL_Rect *input_pad;
    SDL_Rect *btn_up;
    SDL_Rect *btn_down;
    SDL_Rect *btn_left;
    SDL_Rect *btn_right;
    SDL_Rect *btn_a;
    SDL_Rect *btn_b;
    SDL_Rect *btn_start;
    SDL_Rect *btn_select;
    bool dirty;
};

struct DecodedInstruction {
    uint8_t opcode;

    bool has_imm8;
    bool has_imm16;

    bool used_virtual_immediate;

    union {
        uint8_t imm8;
        uint16_t imm16;
    };

    uint16_t pc;
    uint16_t next_pc;
};