/*
 * Copyright (c) 2022 rockchip
 *
 */
#ifndef __RK_EVENT_H__
#define __RK_EVENT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <linux/input.h>

typedef void (*event_handler_t)(struct input_event *);

int rk_event_register(char *dev_path);
int rk_event_unregister(char *dev_path);
int rk_event_listen_start(event_handler_t handler);
int rk_event_listen_stop(void);

#ifdef __cplusplus
}
#endif

#endif //__RK_EVENT_H__

