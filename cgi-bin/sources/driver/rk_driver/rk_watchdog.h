/*
 * Copyright (c) 2022 rockchip
 *
 */

#ifndef __RK_WATCHDOG_H__
#define __RK_WATCHDOG_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

int rk_watchdog_start(int timeval);
int rk_watchdog_stop(void);
int rk_watchdog_refresh(void);

#ifdef __cplusplus
}
#endif

#endif //__RK_WATCHDOG_H__

