/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   blueprint.h
 *
 * Created on 20. října 2025, 12:24
 */

#ifndef BLUEPRINT_H
#define BLUEPRINT_H

#include <stdint.h>

typedef enum {
    PATCH_TYPE_MOV,
    PATCH_TYPE_DAT
} PatchType;

typedef struct _PatchLocation PatchLocation;
typedef struct _KeyRecord BlueprintRecord;

typedef struct _PatchLocation {
    PatchType type;
    uint32_t address;
    uint32_t offset;
    uint32_t len;
    uint64_t imm;
    PatchLocation *next;
} PatchLocation;

typedef struct _KeyRecord {
    uint64_t key;
    char id[64];
    uint32_t raw_len;
    char * encoded_string_raw;
    char * plain_string;
    char * plain_string_raw;
    PatchLocation *patches;
    BlueprintRecord *next;
    uint8_t is_consistent;
} BlueprintRecord;

BlueprintRecord* blueprint_load(const char *filename);

void blueprint_free(BlueprintRecord *head);

void blueprint_print(BlueprintRecord *head);

#endif /* BLUEPRINT_H */

