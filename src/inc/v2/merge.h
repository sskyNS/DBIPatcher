/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   merge.h
 *
 * Created on 20. října 2025, 13:41
 */

#ifndef MERGE_H
#define MERGE_H

#include <stdint.h>
#include <stdio.h>

#define MAX_PLACEHOLDER_LEN 16

typedef struct {
    char value[MAX_PLACEHOLDER_LEN];
} Placeholder;

int placeholder_extract(const char *str, Placeholder * placeholders, uint32_t placeholders_cnt);

uint8_t placeholder_compare(const char *str1, const char *str2);

typedef struct {
    const char * keys;
    FILE * out;
    const char * translation;
} MergeArgs;

int merge(const MergeArgs * args);

#endif /* MERGE_H */

