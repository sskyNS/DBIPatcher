/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   patch2.h
 *
 * Created on 21. října 2025, 11:46
 */

#ifndef PATCH_H
#define PATCH_H

#include <stdint.h>
#include <stdio.h>

#include "../memfile.h"

typedef struct {
    const MemFile * dbi_mf;
    FILE * out;
    const char * blueprint;
    const char * translation;
} PatchArgs;

int patch(const PatchArgs * args);

#endif /* PATCH_H */

