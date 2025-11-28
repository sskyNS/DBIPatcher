/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   keys.h
 *
 * Created on 2. října 2025, 10:32
 */

#ifndef KEYS_H
#define KEYS_H

#include <stdint.h>

#include "memfile.h"

typedef struct {
    uint64_t * keys;
    uint32_t key_cnt;
} KeySet;

void free_key_set(KeySet * ks);

int set_keygen(const char * path);

int set_keygen(const char * path);

KeySet * get_key_set(MemFile * mf);

uint64_t get_key(uint64_t seed);

KeySet * gen_key_set(uint32_t max);

#endif /* KEYS_H */

