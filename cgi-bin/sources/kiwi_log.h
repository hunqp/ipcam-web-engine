/**
 * @file  sys_log.h
 * @brief System journal log writes file, auto clear half 
 * content when reach to full size setting.
 *
 * Version: 1.0
 * Date: 27-08-2025
 * Author: Hunqp
 */
#ifndef SYS_LOG_H
#define SYS_LOG_H

#include <stdio.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SYS_LOG_INFO,
    SYS_LOG_WARN,
    SYS_LOG_DEBUG,
    SYS_LOG_ERROR,
};

#if 0
#define VV_SYSI(fmt, ...)
#define VV_SYSW(fmt, ...)
#define VV_SYSD(fmt, ...)
#define VV_SYSE(fmt, ...)

#define VV_RAM_SYSI(fmt, ...)
#define VV_RAM_SYSW(fmt, ...)
#define VV_RAM_SYSD(fmt, ...)
#define VV_RAM_SYSE(fmt, ...)
#else
#define VV_SYSI(fmt, ...) Kiwi_DIARY_WriteLine(&Kiwi_Journal, SYS_LOG_INFO , fmt, ##__VA_ARGS__)
#define VV_SYSW(fmt, ...) Kiwi_DIARY_WriteLine(&Kiwi_Journal, SYS_LOG_WARN , fmt, ##__VA_ARGS__)
#define VV_SYSD(fmt, ...) Kiwi_DIARY_WriteLine(&Kiwi_Journal, SYS_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define VV_SYSE(fmt, ...) Kiwi_DIARY_WriteLine(&Kiwi_Journal, SYS_LOG_ERROR, fmt, ##__VA_ARGS__)

#define VV_RAM_SYSI(fmt, ...) Kiwi_DIARY_WriteLine(&Kiwi_Runtime, SYS_LOG_INFO , fmt, ##__VA_ARGS__)
#define VV_RAM_SYSW(fmt, ...) Kiwi_DIARY_WriteLine(&Kiwi_Runtime, SYS_LOG_WARN , fmt, ##__VA_ARGS__)
#define VV_RAM_SYSD(fmt, ...) Kiwi_DIARY_WriteLine(&Kiwi_Runtime, SYS_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define VV_RAM_SYSE(fmt, ...) Kiwi_DIARY_WriteLine(&Kiwi_Runtime, SYS_LOG_ERROR, fmt, ##__VA_ARGS__)
#endif

typedef struct {
    const char *filename;
    size_t maxBytesCanBeHold;
    pthread_mutex_t mt;
} KIWI_DIARY_T;

extern KIWI_DIARY_T Kiwi_Runtime;
extern KIWI_DIARY_T Kiwi_Journal;

extern void Kiwi_DIARY_WriteLine(KIWI_DIARY_T *me, int level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* SYS_LOG_H */