/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   strings2.h
 *
 * Created on 8. října 2025, 15:27
 */

#ifndef STRINGS_H
#define STRINGS_H

#include <stdint.h>
#include <stdio.h>

#include "../memfile.h"

typedef enum {
    SCAN_ENGLISH,
    SCAN_RUSSIAN,
} ScanType;

typedef struct {
    const MemFile * dbi_mf;
    const char * lookup;
    FILE * out;
    uint32_t key_cnt;
} ScanStringArgs;

typedef struct {
    ScanType type;
    const MemFile * dbi_mf;
    const char * keys;
    FILE * out;
    uint32_t min_match;
    uint32_t key_cnt;
} ScanTypeArgs;

typedef struct {
    const MemFile * dbi_mf;
    const char * keys;
    FILE * out;
} ScanPartialsArgs;

typedef struct {
    MemFile * dbi_mf;
    const char * keys;
    FILE * out;
} ScanBlueprintArgs;

const char * string_encode(const char * src, uint32_t len);

void string_decode(void * data, uint32_t len);

int scan_string(const ScanStringArgs * args);

int scan_strings_type(const ScanTypeArgs * args);

int scan_partials(const ScanPartialsArgs * args);

int scan_blueprint(const ScanBlueprintArgs * args);

#endif /* STRINGS_H */

