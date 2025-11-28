/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>

#include "log.h"
#include "utils.h"

#define ESC             "\x1B"
#define LOG_SUFFIX      ESC "[0m"
#define LOG_BUFFER      4096

#define LF_CLI          0x01

static const char * app_name = NULL;

static const char * log_get_prefix(LogLevel lvl) {
    switch(lvl) {
        case LOG_TRACE:     return ESC "[1;35m";
        case LOG_DEBUG:     return ESC "[1;34m";
        case LOG_INFO:      return ESC "[0m";
        case LOG_NOTICE:    return ESC "[0;32m";
        case LOG_WARNING:   return ESC "[1;33m";
        case LOG_ERROR:     
        default:            return ESC "[1;31m";
    }
}

static const char * log_get_level(LogLevel lvl) {
    switch(lvl) {
        case LOG_TRACE:     return "TRACE";
        case LOG_DEBUG:     return "DEBUG";
        case LOG_INFO:      return "INFO";
        case LOG_NOTICE:    return "NOTICE";
        case LOG_WARNING:   return "WARNING";
        case LOG_ERROR:     
        default:            return "ERROR";
    }
}

static const char * log_get_time(void) {
    static char timebuff[10];
    
    time_t t;
    struct tm tm;
    
    t = time(NULL);
    tm = *localtime(&t);

    snprintf(timebuff, sizeof(timebuff), "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    
    return timebuff;
}

static void log_write(uint8_t flags, const char * str) {    
    if(flags & LF_CLI) {
        fprintf(stdout, "%s", str);
    }
}

void lf_e(const char * fmt, ...) {
    char tmp[LOG_BUFFER];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    
    fprintf(stderr, "%s: %s%s%s" CRLF, app_name, log_get_prefix(LOG_ERROR), tmp, LOG_SUFFIX);
}

void log_init(const char * app) {
    app_name = app;
}

void log_close(void) {
    
}

void lf_s(LogLevel lvl, const char * fmt, ...) {
    char tmp[LOG_BUFFER];
    va_list args;
    va_start(args, fmt);
    
    log_write(LF_CLI, app_name);
    log_write(LF_CLI, ": ");
    
    log_write(LF_CLI, log_get_prefix(lvl));
    
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    
    log_write(LF_CLI, tmp);
    
    log_write(LF_CLI, LOG_SUFFIX);
    log_write(LF_CLI, CRLF);
    
    va_end(args);
}