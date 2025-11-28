/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   log.h
 *
 * Created on 9. září 2025, 12:25
 */

#ifndef LOG_H
#define LOG_H

#include <stdint.h>

#define CRLF            "\r\n"

typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_NOTICE,
    LOG_WARNING,
    LOG_ERROR,
} LogLevel;

void log_init(const char * app);

void log_close(void);

#define lf_t(fmt,...) lf_s(LOG_TRACE, (fmt), ##__VA_ARGS__)
#define lf_d(fmt,...) lf_s(LOG_DEBUG, (fmt), ##__VA_ARGS__)
#define lf_i(fmt,...) lf_s(LOG_INFO, (fmt), ##__VA_ARGS__)
#define lf_n(fmt,...) lf_s(LOG_NOTICE, (fmt), ##__VA_ARGS__)
#define lf_w(fmt,...) lf_s(LOG_WARNING, (fmt), ##__VA_ARGS__)
//#define lf_e(fmt,...) lf_s(LOG_ERROR, (fmt), ##__VA_ARGS__)

void __attribute__ ((format (printf, 2, 3))) lf_s(LogLevel level, const char * fmt, ...);

void lf_e(const char * fmt, ...);

#endif /* LOG_H */

