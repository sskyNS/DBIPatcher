/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <sys/param.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CANDIDATES  32

#include "v2/imm.h"
#include "v2/inst.h"
#include "log.h"

static void imm_append(ImmResult * res, ImmMatch * imm) {
    imm->next = res->matches;
    res->matches = imm;
    res->matches_cnt++;
}

static ImmMatch * imm_match_init(uint32_t len) {
    ImmMatch * res = malloc(sizeof(*res));
    memset(res, 0, sizeof(*res));
    
    res->cnt = len;
    res->offsets = malloc(sizeof(*res->offsets) * len);
    memset(res->offsets, 0, sizeof(*res->offsets) * len);
    
    return res;
}

static ImmResult * imm_result_init(ImmDefine * def) {
    ImmResult * res = malloc(sizeof(*res));
    memset(res, 0, sizeof(*res));
    
    res->raw_len = def->len - def->offset;
    res->raw = malloc(res->raw_len);
    
    memcpy(res->raw, def->data + def->offset, res->raw_len);
    
    const uint8_t * src = (const uint8_t*)def->data;
    uint8_t * dst = res->raw;
    for(uint32_t x = def->offset; x < def->len; x++) {
        uint8_t xor = (def->key >> ((x & 7) * 8)) & 0xFF;
        *(dst++) = src[x] ^ xor;
    }
    
    return res;
}

ImmResult * imm_lookup(const void * data, uint32_t len, ImmDefine * imm, uint32_t tolerance) {
    const uint8_t * data_u8 = data;
    len = (len / 4) * 4;
    
    uint32_t cand_cnt = ((imm->len - imm->offset) + 1) / 2;
    cand_cnt = MIN(MAX_CANDIDATES, cand_cnt);
    
    ImmResult * res = imm_result_init(imm);
    ImmMatch * match = imm_match_init(cand_cnt);
    
    uint16_t cand_z[cand_cnt];
    memset(cand_z, 0, sizeof(cand_z));
    
    uint16_t cand_n[cand_cnt];
    memset(cand_n, 0xFF, sizeof(cand_n));
    
    const uint8_t * cand = (const uint8_t*)imm->data;
    uint8_t * cand_u8_z = (uint8_t*)cand_z;
    uint8_t * cand_u8_n = (uint8_t*)cand_n;
    for(uint32_t x = imm->offset; x < imm->len; x++) {
        uint8_t xor = (imm->key >> ((x & 7) * 8)) & 0xFF;
        cand_u8_z[x - imm->offset] = cand[x] ^ xor;
        cand_u8_n[x - imm->offset] = cand[x] ^ xor;
    }
    
    uint32_t match_mask = (1UL << cand_cnt) - 1;
    
    uint32_t state_match = 0;
    uint32_t state_mismtach = 0;
    uint32_t state_start = 0;
    
    arm64_instr_t inst;
    uint32_t inst_raw;
    uint16_t inst_imm;
    for(uint32_t i = 0; i < len;) {
        memcpy(&inst_raw, data_u8 + i, 4);
        
        instr_decode(inst_raw, &inst, i);
        
        uint8_t matched = 0;
        switch(inst.type) {
            case INSTR_MOV:
            case INSTR_MOVZ:
            case INSTR_MOVK:
                inst_imm = inst.imm & 0xFFFF;
                
                for(uint32_t x = 0; x < cand_cnt; x++) {
                    uint32_t cand_mask = 0xFFFF;
                    if(x + 1 == cand_cnt && imm->len % 2) {
                        cand_mask = 0xFF;
                    }
                    
                    if((cand_z[x] & cand_mask) == (inst_imm & cand_mask)) {
                        state_match |= (1 << x);
                        match->offsets[x] = i;
                        matched = 1;
                        break;
                    }
                }
                break;
                    
            case INSTR_MOVN:
                inst_imm = inst.imm & 0xFFFF;
                
                for(uint32_t x = 0; x < cand_cnt; x++) {
                    uint32_t cand_mask = 0xFFFF;
                    if(x + 1 == cand_cnt && imm->len % 2) {
                        cand_mask = 0xFF;
                    }
                    
                    if((cand_n[x] & cand_mask) == (inst_imm & cand_mask)) {
                        state_match |= (1 << x);
                        match->offsets[x] = i;
                        matched = 1;
                        break;
                    }
                }
                break;
                
            default:
                break;
        }
        
        if(matched) {
            if(state_match == match_mask) {
                // matched
                //lf_e("matched at 0x%08X", state_start);
                
                imm_append(res, match);
                match = imm_match_init(cand_cnt);
                
                state_match = 0;
                state_mismtach = 0;
                
                state_start = i + 4;
                i = state_start;
                continue;
            } else {
                state_mismtach = 0;
            }
        } else {            
            if(!state_match) {
                state_start = i;
            }
            
            if(++state_mismtach == tolerance) {
                state_match = 0;
                state_mismtach = 0;
                
                state_start += 4;
                i = state_start;
                continue;
            }
        }
        
        i += 4;
    }
    
    imm_match_free(match);
    return res;
}
