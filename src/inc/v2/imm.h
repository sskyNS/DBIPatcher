/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   immediates.h
 *
 * Created on 10. října 2025, 12:11
 */

#ifndef IMMEDIATES_H
#define IMMEDIATES_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    const void * data;
    uint32_t len;
    uint32_t offset;
    uint64_t key;
} ImmDefine;

typedef struct _ImmMatch ImmMatch;

typedef struct _ImmMatch {
    uint32_t cnt;
    uint32_t * offsets;
    ImmMatch * next;
} ImmMatch;

typedef struct _ImmResult ImmResult;

typedef struct _ImmResult {
   uint8_t * raw;
   uint32_t raw_len;
   ImmMatch * matches;
   uint32_t matches_cnt;
   ImmResult * next;
} ImmResult;

ImmResult * imm_scan(const void * data, uint32_t len);

void imm_match_free(ImmMatch * iter);

void imm_result_free(ImmResult * res);

ImmResult * imm_lookup(const void * data, uint32_t len, ImmDefine * imm, uint32_t tolerance);

void imm_print(FILE * f, const ImmDefine * def, const ImmResult * res);

void imm_print_dummy(FILE * f, const ImmDefine * def);

#endif /* IMMEDIATES_H */

