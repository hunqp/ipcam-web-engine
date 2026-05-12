/**
 * @file  cgi_debug.h
 * @brief CGI System log writes file, auto clear half 
 * content when reach to full size setting.
 *
 * Version: 1.0
 * Date: 27-08-2025
 * Author: Hunqp
 */
#ifndef CGI_DEBBUG_H
#define CGI_DEBBUG_H

#include <stdio.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CGI_LOG_INFO,
    CGI_LOG_WARN,
    CGI_LOG_DEBUG,
    CGI_LOG_ERROR,
};

#if 0
#define CGI_SYSI(fmt, ...)
#define CGI_SYSW(fmt, ...)
#define CGI_SYSD(fmt, ...)
#define CGI_SYSE(fmt, ...)

#define CGI_RAM_SYSI(fmt, ...)
#define CGI_RAM_SYSW(fmt, ...)
#define CGI_RAM_SYSD(fmt, ...)
#define CGI_RAM_SYSE(fmt, ...)
#else
#define CGI_SYSI(fmt, ...) CGI_DIARY_WriteLine(&CGI_FLASH, CGI_LOG_INFO , fmt, ##__VA_ARGS__)
#define CGI_SYSW(fmt, ...) CGI_DIARY_WriteLine(&CGI_FLASH, CGI_LOG_WARN , fmt, ##__VA_ARGS__)
#define CGI_SYSD(fmt, ...) CGI_DIARY_WriteLine(&CGI_FLASH, CGI_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define CGI_SYSE(fmt, ...) CGI_DIARY_WriteLine(&CGI_FLASH, CGI_LOG_ERROR, fmt, ##__VA_ARGS__)

#define CGI_RAM_SYSI(fmt, ...) CGI_DIARY_WriteLine(&CGI_RAM, CGI_LOG_INFO , fmt, ##__VA_ARGS__)
#define CGI_RAM_SYSW(fmt, ...) CGI_DIARY_WriteLine(&CGI_RAM, CGI_LOG_WARN , fmt, ##__VA_ARGS__)
#define CGI_RAM_SYSD(fmt, ...) CGI_DIARY_WriteLine(&CGI_RAM, CGI_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define CGI_RAM_SYSE(fmt, ...) CGI_DIARY_WriteLine(&CGI_RAM, CGI_LOG_ERROR, fmt, ##__VA_ARGS__)
#endif

typedef struct {
    const char *filename;
    size_t maxBytesCanBeHold;
    pthread_mutex_t mt;
} CGI_DIARY_T;

extern CGI_DIARY_T CGI_RAM;
extern CGI_DIARY_T CGI_FLASH;

extern void CGI_DIARY_WriteLine(CGI_DIARY_T *me, int level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* CGI_DEBBUG_H */