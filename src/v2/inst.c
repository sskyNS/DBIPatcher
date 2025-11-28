#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "v2/inst.h"
#include "utils.h"
#include "log.h"

// Mostly AI, sorry. Use datasheet if wrong.

#define REG_NUM(r)        ((r) & 0x1F)
#define REG_IS_32(r)      ((r) & REG_W_FLAG)
#define REG_IS_SP(r)      ((r) & REG_SP_FLAG)
#define REG_IS_ZR(r)      ((r) & REG_ZR_FLAG)

// Instruction types
static const char *instr_type_names[] = {
    "unknown", "adrp", "orr", "mov", "movz", "movn", "movk", "str", "strh", "strb",
    "ldr", "stp", "add", "sub", "bl", "b", "dummy"
};

// Register flags
#define REG_W_FLAG    (1 << 7)  // 32-bit if set
#define REG_SP_FLAG   (1 << 8)  // SP/WSP if set
#define REG_ZR_FLAG   (1 << 9)  // ZR/WZR if set

static const char *get_reg_name(arm64_reg_t r) {
    static char buf[4];
    int n = REG_NUM(r);
    if(REG_IS_SP(r)) return REG_IS_32(r) ? "wsp" : "sp";
    if(REG_IS_ZR(r)) return REG_IS_32(r) ? "wzr" : "xzr";
    buf[0] = REG_IS_32(r) ? 'w' : 'x';
    snprintf(&buf[1], sizeof(buf) - 1, "%d", n);
    return buf;
}

// Helper macros
#define BITS(v,h,l) (((v)>>(l)) & ((1u<<((h)-(l)+1))-1))
#define BIT(v,p)    (((v)>>(p)) & 1)

static int64_t signx(uint64_t v, int b) {
    uint64_t s = 1ULL << (b - 1);
    return (v^s)-s;
}

// Decoder signature
typedef int (*dec_fn)(uint32_t, arm64_instr_t*, uint64_t);

// Forward declarations
static int dec_adrp(uint32_t, arm64_instr_t*, uint64_t);
static int dec_mov_w(uint32_t, arm64_instr_t*, uint64_t);
static int dec_ls(uint32_t, arm64_instr_t*, uint64_t);
static int dec_branch(uint32_t, arm64_instr_t*, uint64_t);
static int dec_arith(uint32_t, arm64_instr_t*, uint64_t);
static int dec_stp(uint32_t, arm64_instr_t*, uint64_t);
static int dev_mov(uint32_t, arm64_instr_t *, uint64_t);

// Decoder table
static const dec_fn decoders[] = {
    dec_adrp, dev_mov, dec_mov_w, dec_ls, dec_branch, dec_arith, dec_stp
};
static const int N_DECS = sizeof (decoders) / sizeof (decoders[0]);

// Register constructor

static arm64_reg_t make_register(uint32_t num, int is_x, int allow_sp) {
    if(num == 31) {
        if(allow_sp) return is_x ? REG_SP : REG_WSP;
        else return is_x ? REG_XZR : REG_WZR;
    }
    return is_x ? (arm64_reg_t) num : (arm64_reg_t) (num | REG_W_FLAG);
}

// Master decode

int instr_decode(uint32_t insn, arm64_instr_t *d, uint64_t pc) {
    memset(d, 0, sizeof (*d));
    d->type = INSTR_UNKNOWN;
    for(int i = 0; i < N_DECS; i++) {
        if(decoders[i](insn, d, pc)) return 1;
    }
    return 0;
}

// ADRP

static int dec_adrp(uint32_t i, arm64_instr_t*d, uint64_t pc) {
    if((i & 0x9F000000) != 0x90000000) return 0;
    d->type = INSTR_ADRP;
    d->rd = make_register(BITS(i, 4, 0), 1, 0);
    int lo = BITS(i, 30, 29), hi = BITS(i, 23, 5);
    int64_t off = signx((hi << 2) | lo, 21) << 12;
    d->imm = (pc&~0xFFFULL) + off;
    return 1;
}

// MOV
static int dev_mov(uint32_t i, arm64_instr_t *d, uint64_t pc) {
    uint32_t top10 = i & 0xFFC00000;

    // 32-bit ORR alias (MOV Wd, Wn)
    if (top10 == 0x52000000) {
        // Check shift==LSL#0 and Rm==31
        uint32_t rm   = BITS(i,20,16);
        uint32_t type = BITS(i,23,22);
        uint32_t imm2 = BITS(i,11,10);
        if (rm==31 && type==0 && imm2==0) {
            d->type = INSTR_MOV;
            d->rd   = make_register(BITS(i,4,0), 0, 0);
            d->rn   = make_register(BITS(i,9,5), 0, 1);
            return 1;
        }
    }

    // 64-bit ORR alias (MOV Xd, Xn)
    if (top10 == 0xAA000000) {
        uint32_t rm   = BITS(i,20,16);
        uint32_t type = BITS(i,23,22);
        uint32_t imm2 = BITS(i,11,10);
        if (rm==31 && type==0 && imm2==0) {
            d->type = INSTR_MOV;
            d->rd   = make_register(BITS(i,4,0), 1, 0);
            d->rn   = make_register(BITS(i,9,5), 1, 1);
            return 1;
        }
    }

    // Otherwise, fall through to ORR or MOVZ/MOVN/MOVK, etc.
    return 0;
}

// MOVZ/MOVN/MOVK

static int dec_mov_w(uint32_t i, arm64_instr_t*d, uint64_t pc) {
    if((i & 0x1F800000) != 0x12800000) return 0;
    int sf = BIT(i, 31), opc = BITS(i, 30, 29), hw = BITS(i, 22, 21), imm16 = BITS(i, 20, 5);
    d->rd = make_register(BITS(i, 4, 0), sf, 0);
    d->shift = hw * 16;
    d->imm = imm16;
    if(opc == 0) {
        d->type = INSTR_MOVN;
        d->imm = ~((uint64_t) imm16 << d->shift)&(sf ? ~0ULL : 0xFFFFFFFF);
    } else if(opc == 2) {
        d->type = INSTR_MOVZ;
        d->imm = (uint64_t) imm16 << d->shift;
    } else if(opc == 3) {
        d->type = INSTR_MOVK;
    } else return 0;
    return 1;
}
// Load/store (catch both 0xF9400000 and 0xFD400000 with mask 0xFFC00000)

static int dec_ls(uint32_t i, arm64_instr_t*d, uint64_t pc) {
    uint32_t op = i & 0xFFC00000;
    uint32_t rt = BITS(i,4,0), rn = BITS(i,9,5), imm12 = BITS(i,21,10);

    // STR W (32-bit)
    if (op == 0xB9000000) {
        d->type = INSTR_STR;
        d->rd   = make_register(rt, /*64-bit=*/0, /*sp=*/0);
        d->rn   = make_register(rn, /*64-bit=*/1, /*sp=*/1);
        d->imm  = (uint64_t)imm12 * 4;
        return 1;
    }
    // STR X (64-bit)
    if (op == 0xF9000000) {
        d->type = INSTR_STR;
        d->rd   = make_register(rt, /*64-bit=*/1, /*sp=*/0);
        d->rn   = make_register(rn, /*64-bit=*/1, /*sp=*/1);
        d->imm  = (uint64_t)imm12 * 8;
        return 1;
    }
    // STRH (half-word)
    if (op == 0x79000000) {
        d->type = INSTR_STRH;
        d->rd   = make_register(rt, /*64-bit=*/0, /*sp=*/0);
        d->rn   = make_register(rn, /*64-bit=*/1, /*sp=*/1);
        d->imm  = (uint64_t)imm12 * 2;
        return 1;
    }
    // STRB (byte)
    if (op == 0x39000000) {
        d->type = INSTR_STRB;
        d->rd   = make_register(rt, /*64-bit=*/0, /*sp=*/0);
        d->rn   = make_register(rn, /*64-bit=*/1, /*sp=*/1);
        d->imm  = (uint64_t)imm12;
        return 1;
    }
    // LDR W (32-bit)
    if (op == 0xB9400000) {
        d->type = INSTR_LDR;
        d->rd   = make_register(rt, /*64-bit=*/0, /*sp=*/0);
        d->rn   = make_register(rn, /*64-bit=*/1, /*sp=*/1);
        d->imm  = (uint64_t)imm12 * 4;
        return 1;
    }
    // LDR X or LDR D (64-bit integer or FP)
    if (op == 0xF9400000 || op == 0xFD400000) {
        d->type = INSTR_LDR;
        d->rd   = make_register(rt, /*64-bit=*/1, /*sp=*/0);
        d->rn   = make_register(rn, /*64-bit=*/1, /*sp=*/1);
        d->imm  = (uint64_t)imm12 * 8;
        return 1;
    }
    return 0;
}
// Branch

static int dec_branch(uint32_t i, arm64_instr_t*d, uint64_t pc) {
    if((i & 0xFC000000) == 0x94000000) {
        d->type = INSTR_BL;
        int32_t imm = signx(BITS(i, 25, 0), 26) << 2;
        d->imm = pc + imm;
        return 1;
    }
    if((i & 0xFC000000) == 0x14000000) {
        d->type = INSTR_B;
        int32_t imm = signx(BITS(i, 25, 0), 26) << 2;
        d->imm = pc + imm;
        return 1;
    }
    return 0;
}
// Arithmetic

static int dec_arith(uint32_t i, arm64_instr_t*d, uint64_t pc) {
    int sf = BIT(i, 31), rdn = BITS(i, 4, 0), rnn = BITS(i, 9, 5), imm12 = BITS(i, 21, 10), sh = BIT(i, 22);
    if((i & 0x7FC00000) == 0x11000000) {
        d->type = INSTR_ADD;
        d->rd = make_register(rdn, sf, 1);
        d->rn = make_register(rnn, sf, 1);
        d->imm = imm12 << (sh ? 12 : 0);
        return 1;
    }
    if((i & 0x7FC00000) == 0x51000000) {
        d->type = INSTR_SUB;
        d->rd = make_register(rdn, sf, 1);
        d->rn = make_register(rnn, sf, 1);
        d->imm = imm12 << (sh ? 12 : 0);
        return 1;
    }
    return 0;
}
// Store pair

static int dec_stp(uint32_t i, arm64_instr_t*d, uint64_t pc) {
    int op = i & 0x7EC00000, rta = BITS(i, 4, 0), rtb = BITS(i, 14, 10), rnn = BITS(i, 9, 5);
    int32_t off = signx(BITS(i, 21, 15), 7);
    if(op == 0x29000000) {
        d->type = INSTR_STP;
        d->rd = make_register(rta, 0, 0);
        d->rm = make_register(rtb, 0, 0);
        d->rn = make_register(rnn, 1, 1);
        d->imm = off * 4;
        return 1;
    }
    if(op == 0xA9000000) {
        d->type = INSTR_STP;
        d->rd = make_register(rta, 1, 0);
        d->rm = make_register(rtb, 1, 0);
        d->rn = make_register(rnn, 1, 1);
        d->imm = off * 8;
        return 1;
    }
    return 0;
}

// Print helper

const char *instr_to_string(const arm64_instr_t *d, uint32_t raw, uint64_t pc) {
    static char buf[128];
    int pos = snprintf(buf, sizeof (buf), "0x%08" PRIx64 ": 0x%08" PRIx32 " %-5s ",
                       pc, raw, instr_type_names[d->type]);
    switch(d->type) {
        case INSTR_ADRP:
            pos += snprintf(buf + pos, sizeof (buf) - pos, "%s,0x%" PRIx64, 
                            get_reg_name(d->rd), d->imm);
            break;
        case INSTR_MOVZ: case INSTR_MOVN: case INSTR_MOVK:
            if(d->shift) {
                pos += snprintf(buf + pos, sizeof (buf) - pos, "%s,#0x%" PRIx64 ",LSL#%u", 
                                get_reg_name(d->rd), d->imm, d->shift);
            } else {
                pos += snprintf(buf + pos, sizeof (buf) - pos, "%s,#0x%" PRIx64, 
                                get_reg_name(d->rd), d->imm);
            }
            break;
        case INSTR_STR: case INSTR_STRH: case INSTR_STRB: case INSTR_LDR:
            pos += snprintf(buf + pos, sizeof (buf) - pos, "%s,[%s,#0x%" PRIx64 "]", 
                            get_reg_name(d->rd), get_reg_name(d->rn), d->imm);
            break;
        case INSTR_STP:
            pos += snprintf(buf + pos, sizeof (buf) - pos, "%s,%s,[%s,#0x%" PRIx64 "]", 
                            get_reg_name(d->rd), get_reg_name(d->rm), get_reg_name(d->rn), d->imm);
            break;
        case INSTR_ADD: case INSTR_SUB:
            pos += snprintf(buf + pos, sizeof (buf) - pos, "%s,%s,#0x%" PRIx64, 
                            get_reg_name(d->rd), get_reg_name(d->rn), d->imm);
            break;
        case INSTR_BL: case INSTR_B:
            pos += snprintf(buf + pos, sizeof (buf) - pos, "0x%" PRIx64, d->imm);
            break;
        default:
            pos += snprintf(buf + pos, sizeof (buf) - pos, "(unknown)");
    }
    return buf;
}

////////// patcher

int inst_patch_mov(uint32_t *inst_raw, uint16_t imm_old, uint16_t imm_new, uint32_t length) {
    arm64_instr_t decoded;
    
    if (length < 1 || length > 2) {
        return ERR_MOV_INVALID_LENGTH;
    }
    
    // Decode and validate
    if (!instr_decode(*inst_raw, &decoded, 0)) {
        return ERR_MOV_INVALID;
    }
    
    if (decoded.type != INSTR_MOVZ &&
        decoded.type != INSTR_MOVN &&
        decoded.type != INSTR_MOVK) {
        return ERR_MOV_NOT_MOV;
    }
    
    // Number of bits in the immediate field
    uint32_t bits = (length == 1 ? 8 : 16);
    uint32_t field_lo = 5;
    uint32_t field_hi = field_lo + bits - 1;
    
    // Raw 16-bit immediate from encoding
    uint32_t imm_field = BITS(*inst_raw, field_hi, field_lo);
    
    // For MOVN, the encoded immediate is bitwise-NOT of the true value
    uint32_t imm_current = (decoded.type == INSTR_MOVN)
                         ? ((~imm_field) & ((1u << bits) - 1))
                         : imm_field;
    
    // Check old immediate matches
    if ((imm_current & ((1u << bits) - 1)) != (imm_old & ((1u << bits) - 1))) {
        return ERR_MOV_IMM_MISMATCH;
    }
    
    // Compute new field to encode
    uint32_t imm_new_field = (decoded.type == INSTR_MOVN)
                          ? ((~imm_new) & ((1u << bits) - 1))
                          : (imm_new & ((1u << bits) - 1));
    
    // Clear old immediate bits and insert new field
    uint32_t mask_clear = ~(((1u << bits) - 1) << field_lo);
    uint32_t patched = (*inst_raw & mask_clear)
                     | (imm_new_field << field_lo);
    
    *inst_raw = patched;
    return ERR_MOV_OK;
}