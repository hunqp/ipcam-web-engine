#ifndef RK_PARAM_H
#define RK_PARAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "iniparser.h"

extern dictionary *ini_d;

extern int rk_param_init(char *user_ini_path);
extern int rk_param_deinit();
extern int rk_param_savein();
extern int rk_param_reload();

extern int rk_param_set_int(const char *entry, int val);
extern int rk_param_get_int(const char *entry, int defaultValue);
extern int rk_param_set_string(const char *entry, const char *val);
extern char * rk_param_get_string(const char *entry, const char *defaultValue);
extern int rk_param_unset(const char *entry);

#ifdef __cplusplus
}
#endif

#endif /* RK_PARAM_H */