/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   instructions.h
 *
 * Created on 10. října 2025, 10:25
 */

#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include <stdint.h>

typedef enum {
    INSTR_UNKNOWN = 0,
    INSTR_ADRP,
    INSTR_ORR,
    INSTR_MOV,
    INSTR_MOVZ,
    INSTR_MOVN,
    INSTR_MOVK,
    INSTR_STR,
    INSTR_STRH,
    INSTR_STRB,
    INSTR_LDR,
    INSTR_STP,
    INSTR_ADD,
    INSTR_SUB,
    INSTR_BL,
    INSTR_B,
            
    INSTR_DUMMY,
} arm64_instr_type_t;

#define REG_W_FLAG      (1 << 7)    // 32-bit register if set
#define REG_SP_FLAG     (1 << 8)    // Stack pointer alias
#define REG_ZR_FLAG     (1 << 9)    // Zero register alias

typedef enum {
    // 64-bit registers X0–X30
    REG_X0 = 0, REG_X1 = 1, REG_X2 = 2, REG_X3 = 3,
    REG_X4 = 4, REG_X5 = 5, REG_X6 = 6, REG_X7 = 7,
    REG_X8 = 8, REG_X9 = 9, REG_X10 = 10, REG_X11 = 11,
    REG_X12 = 12, REG_X13 = 13, REG_X14 = 14, REG_X15 = 15,
    REG_X16 = 16, REG_X17 = 17, REG_X18 = 18, REG_X19 = 19,
    REG_X20 = 20, REG_X21 = 21, REG_X22 = 22, REG_X23 = 23,
    REG_X24 = 24, REG_X25 = 25, REG_X26 = 26, REG_X27 = 27,
    REG_X28 = 28, REG_X29 = 29, REG_X30 = 30,

    // 32-bit registers W0–W30
    REG_W0 = REG_W_FLAG | 0, REG_W1 = REG_W_FLAG | 1, REG_W2 = REG_W_FLAG | 2, REG_W3 = REG_W_FLAG | 3,
    REG_W4 = REG_W_FLAG | 4, REG_W5 = REG_W_FLAG | 5, REG_W6 = REG_W_FLAG | 6, REG_W7 = REG_W_FLAG | 7,
    REG_W8 = REG_W_FLAG | 8, REG_W9 = REG_W_FLAG | 9, REG_W10 = REG_W_FLAG | 10, REG_W11 = REG_W_FLAG | 11,
    REG_W12 = REG_W_FLAG | 12, REG_W13 = REG_W_FLAG | 13, REG_W14 = REG_W_FLAG | 14, REG_W15 = REG_W_FLAG | 15,
    REG_W16 = REG_W_FLAG | 16, REG_W17 = REG_W_FLAG | 17, REG_W18 = REG_W_FLAG | 18, REG_W19 = REG_W_FLAG | 19,
    REG_W20 = REG_W_FLAG | 20, REG_W21 = REG_W_FLAG | 21, REG_W22 = REG_W_FLAG | 22, REG_W23 = REG_W_FLAG | 23,
    REG_W24 = REG_W_FLAG | 24, REG_W25 = REG_W_FLAG | 25, REG_W26 = REG_W_FLAG | 26, REG_W27 = REG_W_FLAG | 27,
    REG_W28 = REG_W_FLAG | 28, REG_W29 = REG_W_FLAG | 29, REG_W30 = REG_W_FLAG | 30,

    // Special aliases
    REG_XZR = REG_ZR_FLAG | 31,
    REG_WZR = REG_W_FLAG |  REG_ZR_FLAG | 31,
    REG_SP = REG_SP_FLAG | 31,
    REG_WSP = REG_W_FLAG | REG_SP_FLAG | 31,
} arm64_reg_t;

// Instruction descriptor
typedef struct {
    arm64_instr_type_t type;
    arm64_reg_t rd, rn, rm;
    uint64_t imm;
    uint32_t shift;
} arm64_instr_t;

int instr_decode(uint32_t insn, arm64_instr_t *d, uint64_t pc);

const char * instr_to_string(const arm64_instr_t *d, uint32_t raw, uint64_t pc);

#define ERR_MOV_OK               0   // Successfully patched
#define ERR_MOV_NOT_MOV         -1   // Not a MOVx instruction with immediate
#define ERR_MOV_IMM_MISMATCH    -2   // Immediate value doesn't match imm_old
#define ERR_MOV_INVALID         -3   // Failed to decode instruction
#define ERR_MOV_INVALID_LENGTH  -4   // Invalid length

/**
 * Patch a MOVx instruction's immediate value
 * 
 * This function patches MOVZ, MOVN, and MOVK instructions by replacing
 * the 16-bit immediate field with a new value. It uses the existing
 * instruction decoder to validate the instruction type.
 * 
 * Supported instructions:
 *   - MOVZ (Move with Zero)
 *   - MOVN (Move with NOT)
 *   - MOVK (Move with Keep)
 * 
 * The function only patches the raw 16-bit immediate value stored in 
 * bits 20-5 of the instruction. The shift amount (hw field) is preserved.
 * 
 * @param inst_raw Pointer to the raw 32-bit instruction to patch
 * @param imm_old  Expected old immediate value (16-bit, before any shifts)
 * @param imm_new  New immediate value to set (16-bit, before any shifts)
 * @param length   Length of immediate to check / patch
 * 
 * @return 0 on success, negative error code on failure:
 *         ERR_MOV_NOT_MOV (-1):        Not a MOVx immediate instruction
 *         ERR_MOV_IMM_MISMATCH (-2):   Current immediate doesn't match imm_old
 *         ERR_MOV_INVALID (-3):        Instruction decode failed
 *         ERR_MOV_INVALID_LENGTH (-4): Instruction decode failed
 * 
 * Example:
 *   uint32_t inst = 0xD2824680;  // MOVZ x0, #0x1234
 *   int result = inst_patch_mov(&inst, 0x1234, 0x5678);
 *   // inst now contains 0xD28ACF00 (MOVZ x0, #0x5678)
 */
int inst_patch_mov(uint32_t *inst_raw, uint16_t imm_old, uint16_t imm_new, uint32_t length);

#endif /* INSTRUCTIONS_H */

