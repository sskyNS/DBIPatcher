/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   utf8.h
 *
 * Created on 2. října 2025, 11:28
 */

#ifndef UTF8_H
#define UTF8_H

#include <stdint.h>

#define UT_INVALID  0
#define UT_GENERIC  1
#define UT_CYRILLIC 2
#define UT_LETTER   3
#define UT_NUMBER   4
#define UT_NULL     5

typedef struct {
    uint8_t valid;
    uint32_t next_offset;
} utf8_char_validity;

utf8_char_validity utf8_check_char(const char* str, uint32_t offset);

utf8_char_validity utf8_check_char_unchecked(const char* str, uint32_t offset);

#endif /* UTF8_H */

