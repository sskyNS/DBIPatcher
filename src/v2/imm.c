/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/param.h>

#include "v2/imm.h"
#include "log.h"

void imm_match_free(ImmMatch * iter) {
    while(iter) {
        ImmMatch * m = iter;
        iter = iter->next;
        free(m);
    }
}

void imm_result_free(ImmResult * iter) {
    while(iter) {
        ImmResult * next = iter->next;
        
        free(iter->raw);
        imm_match_free(iter->matches);
        free(iter);
        
        iter = next;
    }
}

void imm_print(FILE * f, const ImmDefine * def, const ImmResult * res) {
    ImmMatch * iter = res->matches;
    while(iter) {

        uint32_t start = UINT32_MAX;
        uint32_t end = 0;
        
        for(uint32_t i = 0; i < iter->cnt; i++) {
            if(iter->offsets[i] < start) {
                start = iter->offsets[i];
            }

             if(iter->offsets[i] > end) {
                end = iter->offsets[i];
            }
        }
        fprintf(f, "// at 0x%08X diff=%u" CRLF, start, end - start);

        for(uint32_t i = 0; i < iter->cnt; i++) {
            uint32_t raw_idx = i * 2;
            uint32_t rem = res->raw_len - raw_idx;
            rem = MIN(rem, 2);

            fprintf(f, "\tmov=0x%08X;offset=%u;len=%u;imm=0x", iter->offsets[i], raw_idx + def->offset, rem);

            for(uint32_t j = rem; j --> 0;) {
                fprintf(f, "%02X", res->raw[raw_idx + j]);
            }
            fprintf(f, CRLF);
        }

        iter = iter->next;
    }
}

void imm_print_dummy(FILE * f, const ImmDefine * def) {

    uint32_t raw_len = (def->len - def->offset);
    uint32_t iter_cnt = (raw_len + 1) / 2;
    
    
    uint8_t raw[def->len];
    const uint8_t * src = (const uint8_t*)def->data;
    uint8_t * dst = raw;
    for(uint32_t x = def->offset; x < def->len; x++) {
        uint8_t xor = (def->key >> ((x & 7) * 8)) & 0xFF;
        *(dst++) = src[x] ^ xor;
    }
    
    fprintf(f, "// at 0x%08X diff=%u" CRLF, UINT32_MAX, 0);

    for(uint32_t i = 0; i < iter_cnt; i++) {
        uint32_t raw_idx = i * 2;
        uint32_t rem = raw_len - raw_idx;
        rem = MIN(rem, 2);

        fprintf(f, "\tmov=0x%08X;offset=%u;len=%u;imm=0x", UINT32_MAX, raw_idx + def->offset, rem);

        for(uint32_t j = rem; j --> 0;) {
            fprintf(f, "%02X", raw[raw_idx + j]);
        }
        fprintf(f, CRLF);
    }
}