#ifndef KIWI_NETWORKS_H
#define KIWI_NETWORKS_H

#include <stdint.h>
#include <net/if.h>
#include <linux/if.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

/* Portable network helpers */
#include "rk_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIWI_NETWORK_ETHR_INTERFACE  (char*)"eth0"
#define KIWI_NETWORK_WIFI_INTERFACE  (char*)"wlan0"

typedef enum {
    KIWI_IF_DOWN = 0,    /* Interface disabled */
    KIWI_IF_DORMANT,     /* Scanning or Connecting */
    KIWI_IF_UP,          /* Connected */
    KIWI_IF_UNKNOWN,     /* Driver doesn't report */
} KIWI_NETWORK_NIC_STATUS;

typedef struct {
    char ssid[32];
    char bssid[18];
    signed char strength;
    char security[32];
} KIWI_NETWORK_WIFI_SCAN_S;

typedef struct {
    char ssid[32];
    char pssk[16];
    int priority;
} KIWI_NETWORK_WIFI_INFO_S;

/**
 * @brief Get the IP4 address of the DNS server.
 * @param dns The IP4 address of the DNS server (in network byte order) - MUST BE 4 BYTES.
 * @return 0 on success, -1 if res_init() fails, -2 if the DNS server address is not found.
 */
extern int Kiwi_DNS_GetAddress(uint8_t *dns, char *stDns);

/**
 * @brief Get the IP4 address of a given interface.
 * @param interface The interface name (e.g. "eth0").
 * @param ip The IP4 address of the interface (in network byte order) - MUST BE 4 BYTES.
 * @param st The IP4 address of the interface in dotted decimal notation (e.g. "192.168.1.1") - MUST BE 16 BYTES.
 * @return 0 on success, -1 if getifaddrs() fails, -2 if no IPv4 addresses are found.
 */
extern int Kiwi_IP4_GetAddress(const char *interface, uint8_t *ip, char *st); 

/**
 * @brief Get the MAC address of a given interface.
 * @param interface The interface name (e.g. "eth0").
 * @param ip The MAC address of the interface (in network byte order) - MUST BE 6 BYTES.
 * @param st The MAC address of the interface in colon-separated hexadecimal notation (e.g. "00:11:22:33:44:55") - MUST BE 18 BYTES.
 * @return 0 on success, -1 if sscanf() fails, -2 if no MAC addresses are found.
 */
extern int Kiwi_MAC_GetAddress(const char *interface, uint8_t *ip, char *st);

/**
 * @brief Get the gateway IP address of a given interface.
 * @param interface The interface name (e.g. "eth0").
 * @param ip The gateway IP address of the interface (in network byte order) - MUST BE 4 BYTES.
 * @param st The gateway IP address of the interface in dotted decimal notation (e.g. "192.168.1.1") - MUST BE 16 BYTES.
 * @return 0 on success, -1 if fopen() fails, -2 if no gateway IP addresses are found.
 */
extern int Kiwi_GATEWAY_GetAddress(const char *interface, uint8_t *ip, char *st, uint8_t *subnetmask, char *stSubnetmask);

/**
 * @brief Get the status of a given network interface.
 * @param interface The network interface name (e.g. "eth0").
 * @return The status of the network interface: IF_UP, IF_DOWN, IF_DORMANT, or IF_UNKNOWN.
 */
extern KIWI_NETWORK_NIC_STATUS Kiwi_NIC_GetStatus(const char *interface);

/**
 * @brief Ping a gateway with the given timeout.
 * @param gateway The IP address of the gateway to ping.
 * @param waitForSeconds The timeout in seconds to wait for a reply.
 * @return true if the ping was successful, false otherwise.
 */
extern bool Kiwi_ROUTE_Ping(const char *gateway, int waitForSeconds);

/**
 * @brief Start a Wi-Fi scan.
 * @return 0 on success, -1 on failure.
 */
extern int Kiwi_WiFi_Scan(void);

/**
 * @brief Get a list of Wi-Fi networks in range.
 * @param list Pointer to an array of KIWI_NETWORK_WIFI_SCAN_S structures.
 * @param len The number of elements in the list.
 * @return The number of Wi-Fi networks found (up to len).
 */
extern int Kiwi_WiFi_GetList(KIWI_NETWORK_WIFI_SCAN_S *list, int len);

/**
 * @brief Check if the given Wi-Fi interface is connected to a network.
 * @param wirelessInterface The name of the Wi-Fi interface to check.
 * @return true if the interface is connected, false otherwise.
 */
extern bool Kiwi_WiFi_IsConnected(const char *wirelessInterface);

/**
 * @brief Get name of Wi-Fi interface is connected to a network.
 * @param wirelessInterface The name of the Wi-Fi interface to check.
 * @return true if the interface is connected, false otherwise.
 */
extern bool Kiwi_WiFi_GetSSIDConnected(const char *wirelessInterface, char *ssid, size_t ssidLen);

/**
 * @brief Start a Wi-Fi network access point with the given SSID and password.
 * @param ssid The SSID of the Wi-Fi network.
 * @param pssk The password of the Wi-Fi network.
 * @return 0 on success, -1 on failure.
 */
extern int Kiwi_WiFi_RunHostapd(const char *ssid, const char *pssk);

/**
 * @brief Start a Wi-Fi network access point with the given SSID and password.
 * @param ssid The SSID of the Wi-Fi network.
 * @param pssk The password of the Wi-Fi network.
 * @return 0 on success, -1 on failure.
 */
extern int Kiwi_WiFi_DoConnect(const char *ssid, const char *pssk);

/**
 * @brief Connect to a list of Wi-Fi networks.
 * @param list Pointer to an array of KIWI_NETWORK_WIFI_INFO_S structures.
 * @param len The number of elements in the list.
 * @return 0 on success, -1 on failure.
 *
 * This function updates the wpa_supplicant.conf file with the given list of Wi-Fi networks
 * and starts a Wi-Fi network connection process in the background.
 */
extern int Kiwi_WiFi_DoConnectList(KIWI_NETWORK_WIFI_INFO_S *list, int len);

/**
 * @brief Close the Wi-Fi interface.
 * @return 0 on success, -1 on failure.
 */
extern int Kiwi_WiFi_ForceClose(void);

/**
 * @brief Start a DNS server with the given domain name.
 * @param domain The domain name to listen for.
 * @return 0 on success, -1 if the domain name is invalid.
 *
 * This function starts a DNS server in the background with the given domain name.
 * The DNS server listens on port 53 and responds to DNS queries for the given domain name.
 * The DNS server is single-threaded and does not handle concurrent requests.
 */
extern int Kiwi_DNS_StartServer(const char *domain);

/**
 * @brief Close the DNS server.
 * @return 0 on success.
 *
 * This function cancels the DNS server thread and sets the thread ID to NULL.
 */
extern int Kiwi_DNS_CloseServer(void);

/**
 * @brief Resolve a domain name to an IPv4 address.
 * @param domain The domain name to resolve.
 * @param ip The resolved IPv4 address (in network byte order) - MUST BE 4 BYTES.
 * @param st The resolved IPv4 address in dotted decimal notation (e.g. "192.168.1.1") - MUST BE 16 BYTES.
 * @return 0 on success, -1 if getaddrinfo() fails, -2 if no IPv4 addresses are found.
 */
extern int Kiwi_DNS_ResolveDomain(const char *domain, uint8_t *ip, char *st);


#ifdef __cplusplus
}
#endif

#endif /* KIWI_NETWORKS_H */

