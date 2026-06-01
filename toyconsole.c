#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include "ops.h"
#include <SDL2/SDL.h>

volatile sig_atomic_t keep_running = 1;
bool vm_halted = false;

SDL_Window* win;
SDL_Surface* surf;

#define SDL_WIDTH 640
#define SDL_HEIGHT 960

void handle_sigint(int sig) {
    keep_running = 0;
}

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
    SDL_Rect rect;
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

void print_registers(struct Registers *regs) {
    printf("PC: 0x%04X\n", regs->pc);
    printf("SP: 0x%04X\n", regs->sp);
    printf("DP: 0x%04X\n", regs->dp);
    printf("AP: 0x%04X\n", regs->ap);
    printf("IN: 0x%02X\n", regs->in);
    printf("FL: 0x%02X\n", regs->fl);
    printf("IM: 0x%04X\n", regs->im);
}

static inline uint8_t mem_read8(union Memory *m, uint16_t addr) {
    return m->bytes[addr % MEM_SIZE];
}

static inline void mem_write8(union Memory *m, uint16_t addr, uint8_t value) {
    m->bytes[addr % MEM_SIZE] = value;
}

static inline uint16_t mem_read16(union Memory *m, uint16_t addr) {
    return
        mem_read8(m, addr) |
        (mem_read8(m, addr + 1) << 8);
}

static inline void mem_write16(union Memory *m, uint16_t addr, uint16_t value) {
    mem_write8(m, addr, value & 0xFF);
    mem_write8(m, addr + 1, (value >> 8) & 0xFF);
}

struct DecodedInstruction fetch(union Memory *memory) {
    struct DecodedInstruction inst = {0};

    inst.pc = memory->registers.pc;
    uint16_t pc = inst.pc;

    inst.opcode = mem_read8(memory, pc++);
    bool vf = memory->registers.fl & FLAG_VF;

    switch (inst.opcode) {
        case OP_PUSH8:
            inst.has_imm8 = true;

            if (vf) {
                inst.used_virtual_immediate = true;
                inst.imm8 = memory->registers.im & 0xFF;
            } else {
                inst.imm8 = mem_read8(memory, pc++);
            }
            break;

        case OP_PUSH16:
        case OP_LOAD8: 
        case OP_LOAD16:
        case OP_VLOAD8: 
        case OP_VLOAD16:
        case OP_STORE8: 
        case OP_STORE16:
        case OP_VSTORE8: 
        case OP_VSTORE16:
        case OP_JMP:
        case OP_JR:
        case OP_JZ:
        case OP_JNZ:
        case OP_JC:
        case OP_JNC:
        case OP_JO:
        case OP_JNO:
        case OP_JS:
        case OP_JNS:
        case OP_CALL:
            inst.has_imm16 = true;

            if (vf) {
                inst.used_virtual_immediate = true;
                inst.imm16 = memory->registers.im;
            } else {
                inst.imm16 =
                    mem_read8(memory, pc) |
                    (mem_read8(memory, pc + 1) << 8);

                pc += 2;
            }
            break;
    }

    inst.next_pc = pc % MEM_SIZE;

    return inst;
}

static int tmp_dbg_initial_sp = 0;

static inline uint8_t rgb332(uint8_t r, uint8_t g, uint8_t b) {
    return (r & 0xE0) | ((g >> 3) & 0x1C) | ((b >> 6) & 0x03);
}


void refresh_display(union Memory *memory, struct GfxHandle *gfx) {
    const uint32_t pixels = DSP_WIDTH * DSP_HEIGHT;

    uint16_t dp = memory->registers.dp % MEM_SIZE;

    static uint8_t temp_buffer[DSP_WIDTH * DSP_HEIGHT];

    uint8_t *vram = memory->bytes;

    if (dp + pixels <= MEM_SIZE) {
        SDL_UpdateTexture(
            gfx->texture,
            NULL,
            vram + dp,
            DSP_WIDTH
        );
    } else {
        uint32_t first_chunk = MEM_SIZE - dp;

        memcpy(temp_buffer, vram + dp, first_chunk);
        memcpy(temp_buffer + first_chunk, vram, pixels - first_chunk);

        SDL_UpdateTexture(
            gfx->texture,
            NULL,
            temp_buffer,
            DSP_WIDTH
        );
    }
}

static inline void internal_writeprevious8(union Memory *memory, int offset, uint8_t value) {
    uint16_t sp = memory->registers.sp;
    if (memory->registers.fl & FLAG_GF) {
        // GF=1: item N at sp-N
        memory->bytes[(sp - offset + MEM_SIZE) % MEM_SIZE] = value;
    } else {
        // GF=0: item N at sp+(N-1)
        memory->bytes[(sp + offset - 1) % MEM_SIZE] = value;
    }
}
 
static inline void internal_writeprevious16(union Memory *memory, int offset, uint16_t value) {
    uint8_t low  = value & 0xFF;
    uint8_t high = (value >> 8) & 0xFF;
    if (memory->registers.fl & FLAG_GF) {
        // GF=1: 16-bit item N: low at sp-N, high at sp-N+1
        memory->bytes[(memory->registers.sp - offset     + MEM_SIZE) % MEM_SIZE] = low;
        memory->bytes[(memory->registers.sp - offset + 1 + MEM_SIZE) % MEM_SIZE] = high;
    } else {
        // GF=0: 16-bit item N: low at sp+N-1, high at sp+N
        memory->bytes[(memory->registers.sp + offset - 1) % MEM_SIZE] = low;
        memory->bytes[(memory->registers.sp + offset    ) % MEM_SIZE] = high;
    }
}
 
static inline uint8_t internal_peek8(union Memory *memory, int offset) {
    uint16_t sp = memory->registers.sp;
    uint16_t addr;

    if (memory->registers.fl & FLAG_GF) {
        // GF=1: SP points one past top. Item N is at sp-N.
        addr = (sp - offset + MEM_SIZE) % MEM_SIZE;
    } else {
        // GF=0: SP points at top. Item N is at sp+(N-1).
        addr = (sp + offset - 1) % MEM_SIZE;
    }
    return memory ->bytes[addr];
}
 
static inline uint16_t internal_peek16(union Memory *memory, int offset) {
    uint16_t sp = memory->registers.sp;
    uint16_t addr;

    if (memory->registers.fl & FLAG_GF) {
        // GF=1: SP points one past top. Item N is at sp-N.
        addr = (sp - offset + MEM_SIZE) % MEM_SIZE;
    } else {
        // GF=0: SP points at top. Item N is at sp+(N-1).
        addr = (sp + offset - 1) % MEM_SIZE;
    }
 
    uint8_t low  = memory->bytes[addr];
    uint8_t high = memory->bytes[(addr + 1) % MEM_SIZE];
 
    return (uint16_t)low | ((uint16_t)high << 8);
}
 
static inline uint8_t internal_pop8(union Memory *m) {
    if (m->registers.fl & FLAG_GF) {
        // GF=1: upward growth. Decrement, then read.
        m->registers.sp = (m->registers.sp - 1 + MEM_SIZE) % MEM_SIZE;
        return m->bytes[m->registers.sp];
    } else {
        // GF=0: downward growth. Read at SP, then increment.
        uint8_t v = m->bytes[m->registers.sp];
        m->registers.sp = (m->registers.sp + 1) % MEM_SIZE;
        return v;
    }
}
 
static inline uint16_t internal_pop16(union Memory *memory) {
    uint16_t sp = memory->registers.sp;
 
    uint8_t low, high;
 
    if (memory->registers.fl & FLAG_GF) {
        // GF=1: upward growth. Decrement by 2, then read low at SP, high at SP+1.
        sp = (sp - 2 + MEM_SIZE) % MEM_SIZE;
        memory->registers.sp = sp;
        low  = memory->bytes[sp % MEM_SIZE];
        high = memory->bytes[(sp + 1) % MEM_SIZE];
    } else {
        // GF=0: downward growth. Read high at SP+1, low at SP, then increment by 2.
        low  = memory->bytes[sp % MEM_SIZE];
        high = memory->bytes[(sp + 1) % MEM_SIZE];
        memory->registers.sp = (sp + 2) % MEM_SIZE;
    }
 
    return (uint16_t)low | ((uint16_t)high << 8);
}
 
static inline void internal_push8(union Memory *memory, uint8_t value) {
    uint16_t sp = memory->registers.sp;
 
    if (memory->registers.fl & FLAG_GF) {
        // GF=1: upward growth. Write at SP, then increment.
        memory->bytes[sp % MEM_SIZE] = value;
        memory->registers.sp = (sp + 1) % MEM_SIZE;
    } else {
        // GF=0: downward growth. Decrement, then write.
        memory->registers.sp = (sp - 1 + MEM_SIZE) % MEM_SIZE;
        memory->bytes[memory->registers.sp] = value;
    }
}
 
static inline void internal_push16(union Memory *memory, uint16_t value) {
    
    uint16_t sp = memory->registers.sp;
    uint8_t low  = value & 0xFF;
    uint8_t high = (value >> 8) & 0xFF;
     
    if (memory->registers.fl & FLAG_GF) {
        // GF=1: upward growth. Write at SP/SP+1, then increment by 2.
        memory->bytes[sp % MEM_SIZE]           = low;
        memory->bytes[(sp + 1) % MEM_SIZE]     = high;
        memory->registers.sp = (sp + 2) % MEM_SIZE;
    } else {
        // GF=0: downward growth. Decrement by 2, then write at new SP/SP+1.
        sp = (sp - 2 + MEM_SIZE) % MEM_SIZE;
        memory->registers.sp = sp;
        memory->bytes[sp % MEM_SIZE]           = low;
        memory->bytes[(sp + 1) % MEM_SIZE]     = high;
    }
}

void debug_dump_stack(union Memory *memory) {
    printf("Stack dump (SP=%04X):\n", memory->registers.sp);
    for (int i = 0; i < 16; i++) {
        uint16_t addr = (memory->registers.sp + i) % MEM_SIZE;
        printf("[%04X]: %02X\n", addr, memory->bytes[addr]);
    }
}

void render_ui(union Memory *memory, struct GfxHandle *gfx) {
    SDL_SetRenderDrawColor(gfx->renderer, 20, 20, 20, 255);
    SDL_RenderClear(gfx->renderer);

    // bezel/background
    SDL_Rect bezel = {
        gfx->rect.x - 8,
        gfx->rect.y - 8,
        gfx->rect.w + 16,
        gfx->rect.h + 16
    };

    SDL_SetRenderDrawColor(gfx->renderer, 80, 80, 80, 255);
    SDL_RenderFillRect(gfx->renderer, &bezel);

    // display
    SDL_RenderCopy(
        gfx->renderer,
        gfx->texture,
        NULL,
        &gfx->rect
    );

    // input pad
    SDL_Rect input_pad = {
        .x = 64,
        .y = gfx->rect.y + gfx->rect.h + 64,
        .w = SDL_WIDTH - 128,
        .h = SDL_HEIGHT - (gfx->rect.y + gfx->rect.h + 128)
    };

    SDL_SetRenderDrawColor(gfx->renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(gfx->renderer, &input_pad);

    SDL_RenderPresent(gfx->renderer);
}

// useful debug fun
void print_stack_segment(union Memory *memory, uint16_t base, int count) {
    printf("Stack segment at base %04X:\n", base);
    printf("SP=%04X\n", memory->registers.sp);
    printf("[%04X]: ", base);
    for (int i = 0; i < count; i++) {
        uint16_t addr = (base + i) % MEM_SIZE;
        printf("%02X ", memory->bytes[addr]);
    }
    printf("\n");
}

void execute(union Memory *memory, struct DecodedInstruction *inst, struct GfxHandle *gfx) {
    switch (inst->opcode) {
        case OP_PUSH8:
            internal_push8(memory, inst->imm8);
            break;

        case OP_PUSH16:
            internal_push16(memory, inst->imm16);
            break;

        case OP_POP8:
            // set im to popped value for potential virtual immediate use
            memory->registers.im = internal_pop8(memory);
            // set vf
            memory->registers.fl |= FLAG_VF;
            break;
        case OP_POP16:
            memory->registers.im = internal_pop16(memory);
            memory->registers.fl |= FLAG_VF;
            break;

        case OP_DROP8:
            internal_pop8(memory);
            break;
        case OP_DROP16:
            internal_pop16(memory);
            break;

        case OP_DUP8:
            {
                internal_push8(memory, internal_peek8(memory, 1));   
            }
            break;

        case OP_DUP16:
            {
                internal_push16(memory, internal_peek16(memory, 2));
            }
            break;
        case OP_SWAP8:
            {
                uint8_t a = internal_peek8(memory, 2);
                uint8_t b = internal_peek8(memory, 1);
                internal_writeprevious8(memory, 2, b);
                internal_writeprevious8(memory, 1, a);
            }
            break;
        case OP_SWAP16:
            {
                uint16_t a = internal_peek16(memory, 4);
                uint16_t b = internal_peek16(memory, 2);
                internal_writeprevious16(memory, 4, b);
                internal_writeprevious16(memory, 2, a);
            }
            break;
        case OP_OVER8:
            {
                internal_push8(memory, internal_peek8(memory, 2));   
            }
            break;

        case OP_ROT8:
            {
                uint8_t a = internal_peek8(memory, 3);
                uint8_t b = internal_peek8(memory, 2);
                uint8_t c = internal_peek8(memory, 1);
                internal_writeprevious8(memory, 3, b);
                internal_writeprevious8(memory, 2, c);
                internal_writeprevious8(memory, 1, a);
            }
            break;
        case OP_LOAD8:
            {
                uint8_t value = memory->bytes[inst->imm16 % MEM_SIZE];
                internal_push8(memory, value);
            }
            break;

        case OP_LOAD16:
            {
                uint16_t addr = inst->imm16;
                uint16_t value = mem_read16(memory, addr);
                internal_push16(memory, value);
            }
            break;

        case OP_STORE8:
            {
                uint8_t value = internal_pop8(memory);
                mem_write8(memory, inst->imm16 % MEM_SIZE, value);
            }
            break;

        case OP_STORE16:
            {
                uint16_t value = internal_pop16(memory);
                mem_write16(memory, inst->imm16 % MEM_SIZE, value);
            }
            break;
        case OP_ADD8:
            {
                uint8_t a = internal_pop8(memory);
                uint8_t b = internal_pop8(memory);
                uint16_t result = a + b;
                internal_push8(memory, result & 0xFF);
                memory->registers.fl &= ~FLAG_CF;
                if (result > 0xFF) {
                    memory->registers.fl |= FLAG_CF;
                }
            }
            break;

        case OP_ADD16:
            {
                uint16_t a = internal_pop16(memory);
                uint16_t b = internal_pop16(memory);
                uint32_t result = a + b;
                internal_push16(memory, result & 0xFFFF);
                memory->registers.fl &= ~FLAG_CF;
                if (result > 0xFFFF) {
                    memory->registers.fl |= FLAG_CF;
                }
            }
            break;
        case OP_SUB8:
            {
                uint8_t b = internal_pop8(memory);
                uint8_t a = internal_pop8(memory);
                uint16_t result = a - b;
                internal_push8(memory, result & 0xFF);
                memory->registers.fl &= ~(FLAG_CF);
                if (a < b)                 memory->registers.fl |= FLAG_CF;
            }
            break;
        case OP_SUB16:
            {
                uint16_t b = internal_pop16(memory);
                uint16_t a = internal_pop16(memory);
                uint32_t result = a - b;
                internal_push16(memory, result & 0xFFFF);
                memory->registers.fl &= ~(FLAG_CF);
                if (a < b)                 memory->registers.fl |= FLAG_CF;
            }
            break;
        case OP_DIV8:
            {
                uint8_t divisor = internal_pop8(memory);
                uint8_t dividend = internal_pop8(memory);
                uint16_t result = (divisor == 0) ? 0 : dividend / divisor;
                internal_push8(memory, result & 0xFF);
                memory->registers.fl &= ~(FLAG_CF);
                if (divisor == 0) memory->registers.fl |= FLAG_CF;
            }
            break;
        case OP_DIV16:
            {
                uint16_t divisor = internal_pop16(memory);
                uint16_t dividend = internal_pop16(memory);
                uint32_t result = (divisor == 0) ? 0 : dividend / divisor;
                internal_push16(memory, result & 0xFFFF);
                memory->registers.fl &= ~(FLAG_CF);
                if (divisor == 0) memory->registers.fl |= FLAG_CF;
            }
            break;

        case OP_MOD8:
            {
                uint8_t divisor = internal_pop8(memory);
                uint8_t dividend = internal_pop8(memory);
                uint16_t result = (divisor == 0) ? 0 : dividend % divisor;
                internal_push8(memory, result & 0xFF);
                memory->registers.fl &= ~(FLAG_CF);
                if (divisor == 0) memory->registers.fl |= FLAG_CF;
            }
            break;
        case OP_CMP8:
            {
                uint8_t a = internal_pop8(memory);
                uint8_t b = internal_pop8(memory);
                uint8_t cmp = (a > b) ? 255 : (a < b) ? 1 : 0;
                if (a < b) memory->registers.fl |= FLAG_CF;
                else       memory->registers.fl &= ~FLAG_CF;
                internal_push8(memory, cmp);
            }
            break;
        case OP_CMP16:
            {
                uint16_t a = internal_pop16(memory);
                uint16_t b = internal_pop16(memory);
                uint8_t cmp = (a > b) ? 255 : (a < b) ? 1 : 0;
                if (a < b) memory->registers.fl |= FLAG_CF;
                else       memory->registers.fl &= ~FLAG_CF;
                internal_push8(memory, cmp);
            }
            break;
        case OP_AND8:
            {
                uint8_t a = internal_pop8(memory);
                uint8_t b = internal_pop8(memory);
                uint8_t result = a & b;
                internal_push8(memory, result);
            }
            break;
        case OP_AND16:
            {
                uint16_t a = internal_pop16(memory);
                uint16_t b = internal_pop16(memory);
                uint16_t result = a & b;
                internal_push16(memory, result);
            }
            break;
        case OP_OR8:
            {
                uint8_t a = internal_pop8(memory);
                uint8_t b = internal_pop8(memory);
                uint8_t result = a | b;
                internal_push8(memory, result);
            }
            break;
        case OP_XOR8:
            {
                uint8_t a = internal_pop8(memory);
                uint8_t b = internal_pop8(memory);
                uint8_t result = a ^ b;
                internal_push8(memory, result);
            }
            break;
        case OP_NOT8:
            {
                internal_push8(memory, ~internal_pop8(memory));
            }
            break;
        case OP_SHL8:
            {
                uint8_t shift = internal_pop8(memory);
                uint8_t value = internal_pop8(memory);

                uint8_t result = value << (shift & 7);
                internal_push8(memory, result);
            }
            break;
        case OP_SHL16:
            {
                uint8_t shift = internal_pop8(memory);
                uint16_t value = internal_pop16(memory);

                uint16_t result = value << (shift & 15);
                internal_push16(memory, result);
            }
            break;
        case OP_SHR8:
            {
                uint8_t shift = internal_pop8(memory);
                uint8_t value = internal_pop8(memory);

                uint8_t result = value >> (shift & 7);
                internal_push8(memory, result);
            }
            break;
        case OP_SHR16:
            {
                uint8_t shift = internal_pop8(memory);
                uint16_t value = internal_pop16(memory);

                uint16_t result = value >> (shift & 15);
                internal_push16(memory, result);
            }
            break;
        case OP_RFD:
            gfx->dirty = true;
            break;
        case OP_HLT:
            vm_halted = true;
            break;
        case OP_NOP:
            break;
        case OP_JMP:
            inst->next_pc = inst->imm16 % MEM_SIZE;
            break;
        case OP_JZ:
            {
                uint8_t cond = internal_pop8(memory);
                if (cond == 0) inst->next_pc = inst->imm16 % MEM_SIZE;
            }
            break;
        case OP_JNZ:
            {
                uint8_t cond = internal_pop8(memory);
                if (cond != 0) inst->next_pc = inst->imm16 % MEM_SIZE;
            }
            break;
        case OP_CALL:
            {
                uint16_t return_addr = inst->next_pc;
                internal_push16(memory, return_addr);
                inst->next_pc = inst->imm16 % MEM_SIZE;
            }
            break;
        case OP_RET:
            {   
                uint16_t return_addr = internal_pop16(memory);
                inst->next_pc = return_addr % MEM_SIZE;
            }
            break;
        case OP_PUSHFL:
            internal_push8(memory, memory->registers.fl);
            break;
        case OP_POPFL:
            memory->registers.fl = internal_pop8(memory);
            break;
        default:
            printf("Unknown opcode: 0x%02X at address 0x%04X\n", inst->opcode, inst->pc);
            printf("Due to incomplete implementation, we halt on unknown instructions so i can chisel out op by op.");
            vm_halted = true;
            break;
    }

    if (inst->used_virtual_immediate) {
        // clear VF after use
        memory->registers.fl &= ~FLAG_VF;
    }
}


void step(union Memory *memory, struct GfxHandle *gfx) {
    struct DecodedInstruction inst = fetch(memory);
    execute(memory, &inst, gfx);
    memory->registers.pc = inst.next_pc;
}

void print_memory(union Memory *memory, uint16_t start, uint16_t end) {
    const int bytes_per_row = 16;

    for (uint16_t addr = start; addr < end; addr += bytes_per_row) {
        printf("0x%04X: ", addr);

        // hex bytes
        for (int i = 0; i < bytes_per_row; i++) {
            uint16_t cur = addr + i;
            if (cur < end) {
                printf("%02X ", memory->bytes[cur]);
            } else {
                printf("   ");
            }
        }

        printf("\n");
    }
}

static double VM_HZ = 1 << 24;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <cartridge.bin> [hz]\n", argv[0]);
        return 1;
    }

    signal(SIGINT, handle_sigint);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("Video driver: %s\n", SDL_GetCurrentVideoDriver());

    win = SDL_CreateWindow(
        "console",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SDL_WIDTH,
        SDL_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!win) {
        printf("Window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!ren) {
        printf("Renderer error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Texture *tex = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGB332,
        SDL_TEXTUREACCESS_STREAMING,
        DSP_WIDTH,
        DSP_HEIGHT
    );

    if (!tex) {
        printf("Texture error: %s\n", SDL_GetError());
        return 1;
    }

    union Memory memory = {0};

    // load cartridge
    FILE *cartridge = fopen(argv[1], "rb");

    if (!cartridge) {
        fprintf(
            stderr,
            "Error: Could not open cartridge file '%s'\n",
            argv[1]
        );
        return 1;
    }

    fread(memory.bytes, 1, MEM_SIZE, cartridge);
    fclose(cartridge);

    if (argc >= 3) {
        VM_HZ = atof(argv[2]);
        if (VM_HZ <= 0) {
            fprintf(stderr, "Error: hz must be positive\n");
            return 1;
        }
        printf("VM speed: %.0f Hz\n", VM_HZ);
    }

    tmp_dbg_initial_sp = memory.registers.sp;

    printf("Cartridge loaded successfully. Starting execution...\n");

    SDL_Rect rect = {
        .x = 64,
        .y = 64,
        .w = DSP_WIDTH * 4,
        .h = DSP_HEIGHT * 4
    };

    struct GfxHandle gfx = {
        .renderer = ren,
        .texture = tex,
        .rect = rect,
        .dirty = true
    };

    SDL_Event e;


    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_counter = SDL_GetPerformanceCounter();

    double cycle_accumulator = 0.0;


    uint32_t last_frame = SDL_GetTicks();
    const uint32_t FRAME_TIME_MS = 16;

    uint64_t total_steps = 0;
    uint64_t vm_cycles = 0;

    uint32_t last_stats = SDL_GetTicks();

    refresh_display(&memory, &gfx);
    gfx.dirty = false;

    while (keep_running) {

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    keep_running = 0;
                    break;
            }
        }

        uint64_t now_counter = SDL_GetPerformanceCounter();

        double elapsed =
            (double)(now_counter - last_counter) /
            (double)perf_freq;

        last_counter = now_counter;

        cycle_accumulator += elapsed * VM_HZ;

        // prevent death spiral after pauses/debugging
        if (cycle_accumulator > VM_HZ * 0.25) {
            cycle_accumulator = VM_HZ * 0.25;
        }

        // execute owed cycles
        while (
            cycle_accumulator >= 1.0 &&
            !vm_halted
        ) {
            step(&memory, &gfx);

            cycle_accumulator -= 1.0;

            total_steps++;
            vm_cycles++;
        }

        if (gfx.dirty) {
            refresh_display(&memory, &gfx);
            gfx.dirty = false;
        }

        uint32_t now_ms = SDL_GetTicks();

        if (now_ms - last_frame >= FRAME_TIME_MS) {

            render_ui(&memory, &gfx);

            last_frame = now_ms;
        }

        if (now_ms - last_stats >= 2000) {

            double seconds =
                (now_ms - last_stats) / 1000.0;

            double ips =
                (double)total_steps / seconds;

            printf(
                "VM: %.0f instr/sec | %.2f MHz | halted=%d\n",
                ips,
                ips / 1000000.0,
                vm_halted
            );

            total_steps = 0;
            last_stats = now_ms;
        }

        // tiny sleep so we don't spin at 100% CPU
        SDL_Delay(1);
    }

    FILE *mem_dump = fopen("memory_dump.bin", "wb");

    if (!mem_dump) {
        fprintf(
            stderr,
            "Error: Could not open memory dump file for writing\n"
        );
        return 1;
    }

    fwrite(memory.bytes, 1, MEM_SIZE, mem_dump);
    fclose(mem_dump);

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);

    SDL_Quit();

    return 0;
}