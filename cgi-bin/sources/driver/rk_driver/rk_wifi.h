#ifndef __RK_WIFI_H__
#define __RK_WIFI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern int rk_wiFi_start_ap(const char *interface, const char *ssid, const char *pssk);
extern int rk_wiFi_start_sta(void);
extern int rk_wiFi_start_dns(const char *domain);
extern int rk_wiFi_close_dns(void);
extern int rk_wiFi_get_mac(const char *interface, uint8_t mac[6]);
extern int rk_wiFi_get_ip4(const char *interface, uint8_t ip4[4]);

#ifdef __cplusplus
}
#endif

#endif //__RK_WIFI_H__