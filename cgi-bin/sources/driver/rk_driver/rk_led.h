/*
 * Copyright (c) 2022 rockchip
 *
 */

#ifndef __RK_LED_H__
#define __RK_LED_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>

int rk_led_set_mode(char *dev_path, bool blink, uint32_t brightness);

#ifdef __cplusplus
}
#endif

#endif //__RK_LED_H__
