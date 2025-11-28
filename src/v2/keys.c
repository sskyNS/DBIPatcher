/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

#include "memfile.h"
#include "utils.h"
#include "log.h"
#include "v2/keys.h"


/* 
 * File:   keys.c
 *
 * Created on 2. října 2025, 10:32
 */

#define MAX_KEYS        (1024 * 1024)
      
#define INST_MOV        0xD2800000
#define INST_MOV_MASK   0xFFE00000

#define INST_MOVK       0xF2800000
#define INST_MOVK_MASK  0xFF800000

typedef enum {
    IT_NONE = 0,
    IT_MOV,
    IT_MOVK,
} InstructionType;

typedef struct {
    InstructionType type;
    
    uint16_t immediate;
    uint8_t reg;
    uint8_t shift;
} Arm64Instruction;

typedef struct {
    InstructionType type;
    uint8_t shift;
} InstructionSequence;

// NOTE: 810+: decryption key is in most cases constructed by following instruction .
// pattern given the fact it based on PRNG, we should be able to catch all 
// relevant keys.

// NOTE: 846+: finally does it properly and not based on line numbers, you can
// get the keys, translate at least long/partial strings. Short would require
// manual key recovery?
static const InstructionSequence key_instructions[] = {
    { IT_MOV,   0 },
    { IT_MOVK,  16 },
    { IT_MOVK,  32 },
    { IT_MOVK,  48 },
};

static uint64_t * keygen = NULL;
static uint32_t keygen_len = 0;

static Arm64Instruction decode_instruction(uint32_t raw) {
    if((raw & INST_MOV_MASK) == INST_MOV) {
        return (Arm64Instruction){
            .type = IT_MOV,
            .immediate = (raw >> 5) & 0xFFFF,
            .reg = raw & 0x1F,
            .shift = 0,
        };
    }
    
    if((raw & INST_MOVK_MASK) == INST_MOVK) {
        return (Arm64Instruction){
            .type = IT_MOVK,
            .immediate = (raw >> 5) & 0xFFFF,
            .reg = raw & 0x1F,
            .shift = ((raw >> 21) & 0x3) * 16,
        };
    }
    
    return (Arm64Instruction){
        .type = IT_NONE,
    };
}

void free_key_set(KeySet * ks) {
    if(ks) {
        free(ks->keys);
        free(ks);
    }
}

int set_keygen(const char * path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return EXIT_FAILURE;
    }

    char line[64];
    
    // yea, I know, just wanted to quickly check
    uint64_t * keys = calloc(MAX_KEYS, sizeof(uint64_t));
    uint64_t * key_ptr = keys;
    
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        
        // skip empty lines and comments
        if (line[0] == '\0') {
            continue;
        }
        
        char * start = strstr(line, "0x");
        if(start) {
            start += 2;
            
            if(key_ptr != keys + MAX_KEYS) {
                *(key_ptr++) = strtoul(start, NULL, 16);
            }
        }
    }
    
    keygen = keys;
    keygen_len = key_ptr - keys;

    fclose(fp);
    return EXIT_SUCCESS;
}

KeySet * get_key_set(MemFile * mf) {
    // yea, I know, just wanted to quickly check
    uint64_t * keys = calloc(MAX_KEYS, sizeof(uint64_t));
    uint64_t * key_ptr = keys;
    *(key_ptr++) = 0;
        
    const InstructionSequence * state = key_instructions;
    uint64_t key = 0;
    uint8_t reg = 0;
    
    uint32_t max_len = (mf->len / 4) * 4;
    for(uint32_t i = 0; i < max_len; i += 4) {
        uint32_t cur;
        memcpy(&cur, mf->data + i, 4);
        
        Arm64Instruction inst = decode_instruction(cur);
        if(inst.type == state->type && inst.shift == state->shift) {
            key |= (uint64_t)inst.immediate << inst.shift;
            
            if(state == key_instructions) {
                reg = inst.reg;
            } else if(reg != inst.reg) {
                goto skip;
            }
            
            if(++state - key_instructions == ARRLEN(key_instructions)) {

                uint64_t * iter = keys;
                while(iter != key_ptr) {
                    if(*iter == key) {
                        break;
                    }
                    iter++;
                }
                
                if(iter == key_ptr) {
                    if(key_ptr - keys != MAX_KEYS) {
                        *(key_ptr++) = key;
                        
                        //lf_i("found key %u: 0x%016" PRIx64 " @ 0x%08X", key_ptr - keys, key, i - 12);
                    }
                }

                state = key_instructions;
                key = 0;
            }
        } else {
        skip:
            if(state != key_instructions) {
                i -= (state - key_instructions) * 4;
                
                state = key_instructions;
                key = 0;
            }
        }
    }
    
    KeySet * ks = malloc(sizeof(KeySet));
    memset(ks, 0, sizeof(*ks));
    
    ks->keys = keys;
    ks->key_cnt = key_ptr - keys;
    
    return ks;
}

// NOTE 810+:
// https://github.com/adamyaxley/Obfuscate/blob/master/obfuscate.h
// NOTE 846+: something else or better seed
uint64_t get_key(uint64_t seed) {
    // real 0 is not used anywhere
    if(seed == 0) {
        return 0;
    }
    
    if(keygen_len != 0) {
        if(seed < keygen_len) {
            return keygen[seed];
        }
    }
    
    // Use the MurmurHash3 64-bit finalizer to hash our seed
    uint64_t key = seed;
    key ^= (key >> 33);
    key *= 0xff51afd7ed558ccd;
    key ^= (key >> 33);
    key *= 0xc4ceb9fe1a85ec53;
    key ^= (key >> 33);

    // Make sure that a bit in each byte is set
    key |= 0x0101010101010101ull;
    
    return key;
}

KeySet * gen_key_set(uint32_t max) {
    KeySet * ks = malloc(sizeof(KeySet));
    memset(ks, 0, sizeof(*ks));
    
    if(max != 0) { 
        max += 1;
        
        ks->keys = calloc(max, sizeof(uint64_t));
        ks->key_cnt = max;

        for(uint32_t i = 0; i < max; i++) {
            ks->keys[i] = get_key(i);
        }
    } else {
        ks->keys = calloc(1, sizeof(uint64_t));
        ks->key_cnt = 1;
    }
    
    return ks;
}