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

static inline uint16_t sp_add(union Memory *memory, int16_t n) {
    bool gf = memory->registers.fl & FLAG_GF;
    if (gf) {
        return (memory->registers.sp + n) % MEM_SIZE;
    } else {
        return (memory->registers.sp - n + MEM_SIZE) % MEM_SIZE;
    }
}
static inline uint16_t sp_peek(union Memory *memory, uint16_t offset) {
    bool gf = memory->registers.fl & FLAG_GF;
    if (gf) {
        return (memory->registers.sp + MEM_SIZE - offset) % MEM_SIZE;
    } else {
        return (memory->registers.sp + offset) % MEM_SIZE;
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

void execute(union Memory *memory, struct DecodedInstruction *inst, struct GfxHandle *gfx) {
    // draw red x to vram

    
    switch (inst->opcode) {
        case OP_DBG:
            print_registers(&memory->registers);
            break;
        case OP_PUSH8:
            memory->bytes[memory->registers.sp] = inst->imm8;
            memory->registers.sp = sp_add(memory, 1);
            break;

        case OP_PUSH16:
            memory->bytes[memory->registers.sp] = inst->imm16 & 0xFF;
            memory->bytes[sp_add(memory, 1)] = (inst->imm16 >> 8) & 0xFF;
            memory->registers.sp = sp_add(memory, 2);
            break;

        case OP_DROP8:
            memory->registers.sp = sp_add(memory, -1);
            break;
        case OP_DROP16:
            memory->registers.sp = sp_add(memory, -2);
            break;

        case OP_DUP8:
            {
                uint8_t value = memory->bytes[sp_peek(memory, 1)];
                memory->bytes[memory->registers.sp] = value;
                memory->registers.sp = sp_add(memory, 1);
            }
            break;

        case OP_DUP16:
            {
                uint8_t low  = memory->bytes[sp_peek(memory, 2)];
                uint8_t high = memory->bytes[sp_peek(memory, 1)];
                memory->bytes[memory->registers.sp]             = low;
                memory->bytes[sp_add(memory, 1)]                = high;
                memory->registers.sp = sp_add(memory, 2);
            }
            break;
        case OP_OVER8:
            {
                uint8_t value = memory->bytes[sp_peek(memory, 2)];
                memory->bytes[memory->registers.sp] = value;
                memory->registers.sp = sp_add(memory, 1);
            }
            break;

        case OP_ROT8:
            {
                uint8_t a = memory->bytes[sp_peek(memory, 3)];
                uint8_t b = memory->bytes[sp_peek(memory, 2)];
                uint8_t c = memory->bytes[sp_peek(memory, 1)];

                memory->bytes[sp_peek(memory, 3)] = b;
                memory->bytes[sp_peek(memory, 2)] = c;
                memory->bytes[sp_peek(memory, 1)] = a;
            }
            break;
        case OP_LOAD8:
            {
                uint8_t value = memory->bytes[inst->imm16 % MEM_SIZE];
                memory->bytes[memory->registers.sp] = value;
                memory->registers.sp = sp_add(memory, 1);
            }
            break;

        case OP_LOAD16:
            {
                uint16_t addr = inst->imm16;
                uint16_t value = mem_read16(memory, addr);
                memory->bytes[memory->registers.sp]  = value & 0xFF;
                memory->bytes[sp_add(memory, 1)]     = (value >> 8) & 0xFF;
                memory->registers.sp = sp_add(memory, 2);
            }
            break;

        case OP_STORE8:
            {
                uint8_t value = memory->bytes[sp_peek(memory, 1)];
                mem_write8(memory, inst->imm16 % MEM_SIZE, value);
                memory->registers.sp = sp_add(memory, -1);
            }
            break;

        case OP_STORE16:
            {
                uint8_t low  = memory->bytes[sp_peek(memory, 2)];
                uint8_t high = memory->bytes[sp_peek(memory, 1)];

                mem_write16(memory, inst->imm16, (high << 8) | low);

                memory->registers.sp = sp_add(memory, -2);
            }
            break;
        case OP_ADD8:
            {
                uint8_t a = memory->bytes[sp_peek(memory, 2)];
                uint8_t b = memory->bytes[sp_peek(memory, 1)];
                uint16_t result = a + b;
                memory->bytes[sp_peek(memory, 2)] = result & 0xFF;
                memory->registers.sp = sp_add(memory, -1);
                memory->registers.fl &= ~(FLAG_CF);
                if (result > 0xFF)         memory->registers.fl |= FLAG_CF;
            }
            break;

        case OP_ADD16:
            {
                uint16_t a = memory->bytes[sp_peek(memory, 4)] | (memory->bytes[sp_peek(memory, 3)] << 8);
                uint16_t b = memory->bytes[sp_peek(memory, 2)] | (memory->bytes[sp_peek(memory, 1)] << 8);
                uint32_t result = a + b;
                memory->bytes[sp_peek(memory, 4)] = result & 0xFF;
                memory->bytes[sp_peek(memory, 3)] = (result >> 8) & 0xFF;
                memory->registers.sp = sp_add(memory, -2);
                memory->registers.fl &= ~(FLAG_CF);
                if (result > 0xFFFF)          memory->registers.fl |= FLAG_CF;
            }
            break;
        case OP_CMP8:
            {
                uint8_t a = memory->bytes[sp_peek(memory, 1)];
                uint8_t b = memory->bytes[sp_peek(memory, 2)];
                memory->registers.sp = sp_add(memory, -2);
                uint8_t cmp = (a > b) ? 255 : (a < b) ? 1 : 0;
                if (a < b) memory->registers.fl |= FLAG_CF;
                else       memory->registers.fl &= ~FLAG_CF;
                memory->bytes[memory->registers.sp] = cmp;
                memory->registers.sp = sp_add(memory, 1);
            }
            break;
        case OP_CMP16:
            {
                uint16_t a = memory->bytes[sp_peek(memory, 4)] | (memory->bytes[sp_peek(memory, 3)] << 8);
                uint16_t b = memory->bytes[sp_peek(memory, 2)] | (memory->bytes[sp_peek(memory, 1)] << 8);
                memory->registers.sp = sp_add(memory, -4);
                uint8_t cmp = (a > b) ? 255 : (a < b) ? 1 : 0;
                if (a < b) memory->registers.fl |= FLAG_CF;
                else       memory->registers.fl &= ~FLAG_CF;
                memory->bytes[memory->registers.sp] = cmp;
                memory->registers.sp = sp_add(memory, 1);
            }
            break;
        case OP_AND8:
            {
                uint8_t a = memory->bytes[sp_peek(memory, 2)];
                uint8_t b = memory->bytes[sp_peek(memory, 1)];
                uint8_t result = a & b;
                memory->bytes[sp_peek(memory, 2)] = result;
                memory->registers.sp = sp_add(memory, -1);
            }
            break;
        case OP_OR8:
            {
                uint8_t a = memory->bytes[sp_peek(memory, 2)];
                uint8_t b = memory->bytes[sp_peek(memory, 1)];
                uint8_t result = a | b;
                memory->bytes[sp_peek(memory, 2)] = result;
                memory->registers.sp = sp_add(memory, -1);
            }
            break;
        case OP_SHR8:
            {
                uint8_t value = memory->bytes[sp_peek(memory, 2)];
                uint8_t shift = memory->bytes[sp_peek(memory, 1)];

                uint8_t result = value >> (shift & 7);
                memory->bytes[sp_peek(memory, 2)] = result;
                memory->registers.sp = sp_add(memory, -1);
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
                uint8_t cond = memory->bytes[sp_peek(memory, 1)];
                memory->registers.sp = sp_add(memory, -1);
                if (cond == 0) inst->next_pc = inst->imm16 % MEM_SIZE;
            }
            break;
        case OP_JNZ:
            {
                uint8_t cond = memory->bytes[sp_peek(memory, 1)];
                memory->registers.sp = sp_add(memory, -1);
                if (cond != 0) inst->next_pc = inst->imm16 % MEM_SIZE;
            }
            break;
        default:
            printf("implicit NOP: 0x%02X\n", inst->opcode);
            // vm_halted = true;
            break;
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

const double VM_HZ = 1 << 24;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <cartridge.bin>\n", argv[0]);
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

    // ---- VM timing ----


    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_counter = SDL_GetPerformanceCounter();

    double cycle_accumulator = 0.0;

    // ---- UI timing ----

    uint32_t last_frame = SDL_GetTicks();
    const uint32_t FRAME_TIME_MS = 16;

    // ---- Stats ----

    uint64_t total_steps = 0;
    uint64_t vm_cycles = 0;

    uint32_t last_stats = SDL_GetTicks();

    // ---- Initial framebuffer upload ----

    refresh_display(&memory, &gfx);
    gfx.dirty = false;

    // =========================================================
    // Main loop
    // =========================================================

    while (keep_running) {

        // -----------------------------------------------------
        // Events
        // -----------------------------------------------------

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    keep_running = 0;
                    break;
            }
        }

        // -----------------------------------------------------
        // VM clocking
        // -----------------------------------------------------

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

        // -----------------------------------------------------
        // Upload framebuffer
        // -----------------------------------------------------

        if (gfx.dirty) {
            refresh_display(&memory, &gfx);
            gfx.dirty = false;
        }

        // -----------------------------------------------------
        // Stable UI rendering
        // -----------------------------------------------------

        uint32_t now_ms = SDL_GetTicks();

        if (now_ms - last_frame >= FRAME_TIME_MS) {

            render_ui(&memory, &gfx);

            last_frame = now_ms;
        }

        // -----------------------------------------------------
        // Stats
        // -----------------------------------------------------

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

    // =========================================================
    // Shutdown
    // =========================================================

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