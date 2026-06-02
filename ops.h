#pragma once

#include <stdint.h>
#include <stdbool.h>

#define IN_UP            (1 << 0)
#define IN_DOWN          (1 << 1)
#define IN_LEFT          (1 << 2)
#define IN_RIGHT         (1 << 3)
#define IN_A             (1 << 4)
#define IN_B             (1 << 5)
#define IN_START         (1 << 6)
#define IN_SELECT        (1 << 7)

typedef enum {
    OP_DBG      = 0x00,
    // Stack
    OP_PUSH8    = 0x11, OP_PUSH16   = 0x21,
    OP_PEEK8    = 0x12, OP_PEEK16   = 0x22,
    OP_POP8     = 0x13, OP_POP16    = 0x23,
    OP_DROP8    = 0x14, OP_DROP16   = 0x24,
    OP_DUP8     = 0x15, OP_DUP16    = 0x25,
    OP_SWAP8    = 0x16, OP_SWAP16   = 0x26,
    OP_OVER8    = 0x17, OP_OVER16   = 0x27,
    OP_ROT8     = 0x18, OP_ROT16    = 0x28,
    OP_PUSHSP   = 0x29, OP_POPSP    = 0x2A,
    // Memory
    OP_LOAD8    = 0x31, OP_LOAD16   = 0x41,
    OP_VLOAD8   = 0x32, OP_VLOAD16  = 0x42,
    OP_STORE8   = 0x33, OP_STORE16  = 0x43,
    OP_VSTORE8  = 0x34, OP_VSTORE16 = 0x44,
    // Arithmetic
    OP_ADD8     = 0x51, OP_ADD16    = 0x61,
    OP_SUB8     = 0x52, OP_SUB16    = 0x62,
    OP_MUL8     = 0x53, OP_MUL16    = 0x63,
    OP_DIV8     = 0x54, OP_DIV16    = 0x64,
    OP_MOD8     = 0x55, OP_MOD16    = 0x65,
    OP_CMP8     = 0x56, OP_CMP16    = 0x66,
    // Bitwise
    OP_AND8     = 0x71, OP_AND16    = 0x81,
    OP_OR8      = 0x72, OP_OR16     = 0x82,
    OP_XOR8     = 0x73, OP_XOR16    = 0x83,
    OP_NOT8     = 0x74, OP_NOT16    = 0x84,
    OP_SHL8     = 0x75, OP_SHL16    = 0x85,
    OP_SHR8     = 0x76, OP_SHR16    = 0x86,
    OP_ROL8     = 0x77, OP_ROL16    = 0x87,
    OP_ROR8     = 0x78, OP_ROR16    = 0x88,
    // System
    OP_RFD      = 0x90, OP_RFA      = 0x91,
    OP_INP      = 0x92, OP_HLT      = 0x93, OP_WAIT = 0x94,
    // Control
    OP_NOP      = 0xA0, OP_JMP      = 0xA1, OP_JR   = 0xA2,
    OP_JZ       = 0xA3, OP_JNZ      = 0xA4,
    OP_JC       = 0xA5, OP_JNC      = 0xA6,
    OP_JO       = 0xA7, OP_JNO      = 0xA8,
    OP_JS       = 0xA9, OP_JNS      = 0xAA,
    OP_CALL     = 0xAB, OP_RET      = 0xAC,
    OP_PUSHPC   = 0xAD, OP_PUSHFL   = 0xAE, OP_POPFL = 0xAF
} vc_opcode;

typedef struct {
    uint16_t wave_ptr;  // bit 0-15
    uint16_t frequency; // bit 16-31
    uint16_t phase;     // bit 32-47
    uint8_t  volume : 4; // bit 48-51
    uint8_t  pan    : 4; // bit 52-55
    uint8_t  enabled : 1; // bit 56
    uint8_t  loop    : 1; // bit 57
    uint8_t  pingpong: 1; // bit 58
    uint8_t  noise   : 1; // bit 59
    uint8_t  reset   : 1; // bit 60
    uint8_t  reserved: 3; // bit 61-63
} vc_voice;

