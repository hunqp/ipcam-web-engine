#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

/* Network libraries */
#include <netdb.h>
#include <resolv.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/wireless.h> 

#include <net/if.h>
#include <linux/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "network.h"

static void runCommand(char *cmdline, char *buffer, int bufferLen) {
    if (!cmdline) {
        return;
    }

    FILE *fp = NULL;
    char *p = buffer;
    memset(buffer, 0, bufferLen);
    if ((fp = popen(cmdline, "r")) != NULL) {
        while (fgets(p, bufferLen, fp)) {
            p += strlen(p);
            bufferLen -= strlen(p);
            if (bufferLen <= 1) {
                break;
            }
        }
    }
    pclose(fp);
}

/**
 * @brief Get the IP4 address of the DNS server.
 * @param dns The IP4 address of the DNS server (in network byte order) - MUST BE 4 BYTES.
 * @return 0 on success, -1 if res_init() fails, -2 if the DNS server address is not found.
 */
int Kiwi_DNS_GetAddress(uint8_t *dns, char *stDns) {
    if (res_init() != 0) {
        return -1;
    }
    if (_res.nscount > 0) {
        struct sockaddr_in *addr = (struct sockaddr_in *)&_res.nsaddr_list[0];
        if (dns) {
            memcpy(dns, &addr->sin_addr, 4);
        }
        if (stDns) {
            inet_ntop(AF_INET, &addr->sin_addr, stDns, 16);
        }
        return 0;
    }
    return -2;
}

/**
 * @brief Get the IP4 address of a given interface.
 * @param interface The interface name (e.g. "eth0").
 * @param ip The IP4 address of the interface (in network byte order) - MUST BE 4 BYTES.
 * @param st The IP4 address of the interface in dotted decimal notation (e.g. "192.168.1.1") - MUST BE 16 BYTES.
 * @return 0 on success, -1 if getifaddrs() fails, -2 if no IPv4 addresses are found.
 */
int Kiwi_IP4_GetAddress(const char *interface, uint8_t *ip, char *st) {
    bool boolean = false;
    struct ifaddrs *list, *p;
    struct sockaddr_in *sa = NULL;

    if (getifaddrs(&list) == -1) {
        return -1;
    }

    for (p = list; (p != NULL) && (!boolean); p = p->ifa_next) {
        if (p->ifa_addr == NULL) {
			continue;
		}
        if ((strcmp(p->ifa_name, interface) == 0) && (p->ifa_addr->sa_family == AF_INET)) {
            sa = (struct sockaddr_in *)p->ifa_addr;
            /* IP4 Address */
            if (ip) {
                memcpy(ip, &sa->sin_addr, 4);
            }
            if (st) {
                inet_ntop(AF_INET, &sa->sin_addr, st, 16);
            }
            boolean = true;
        }
    }

    freeifaddrs(list);

    return (boolean ? 0 : -2);
}

/**
 * @brief Get the MAC address of a given interface.
 * @param interface The interface name (e.g. "eth0").
 * @param ip The MAC address of the interface (in network byte order) - MUST BE 6 BYTES.
 * @param st The MAC address of the interface in colon-separated hexadecimal notation (e.g. "00:11:22:33:44:55") - MUST BE 18 BYTES.
 * @return 0 on success, -1 if sscanf() fails, -2 if no MAC addresses are found.
 */
int Kiwi_MAC_GetAddress(const char *interface, uint8_t *ip, char *st) {
    char chars[32] = { 0 };
    snprintf(chars, sizeof(chars), (const char *)"/sys/class/net/%s/address", interface);

    FILE *fp = fopen(chars, "r");
    if (fp) {
        memset(chars, 0, sizeof(chars));
        while (fgets(chars, sizeof(chars), fp));
        fclose(fp);
        unsigned int values[6] = {0};
        if (sscanf(chars, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) != 6) {
            return -1;
        }

        if (ip) {
            for (int i = 0; i < 6; ++i) {
                ip[i] = (uint8_t)values[i];
            }
        }
        if (st) {
            snprintf(st, 18, "%02X:%02X:%02X:%02X:%02X:%02X", values[0], values[1], values[2], values[3], values[4], values[5]);
        }
        return 0;
    }
    return -2;
}

/**
 * @brief Get the gateway IP address of a given interface.
 * @param interface The interface name (e.g. "eth0").
 * @param ip The gateway IP address of the interface (in network byte order) - MUST BE 4 BYTES.
 * @param st The gateway IP address of the interface in dotted decimal notation (e.g. "192.168.1.1") - MUST BE 16 BYTES.
 * @return 0 on success, -1 if fopen() fails, -2 if no gateway IP addresses are found.
 */
int Kiwi_GATEWAY_GetAddress(const char *interface, uint8_t *ip, char *st, uint8_t *subnetmask, char *stSubnetmask) {
    FILE *fp = fopen((const char*)"/proc/net/route", "r");
    if (!fp) {
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char iFace[64] = {0};
        unsigned long dest, gw;
        int flags, refcnt, use, metric, mask;
        if (sscanf(line, "%63s %lx %lx %x %d %d %d %x", iFace, &dest, &gw, &flags, &refcnt, &use, &metric, &mask) != 8) {
            continue;
        }
        if (dest == 0 && strcmp(iFace, interface) == 0) {
            /* IP Gateway */
            if (ip) {
                ip[0] = (gw & 0xFF);
                ip[1] = (gw >> 8) & 0xFF;
                ip[2] = (gw >> 16) & 0xFF;
                ip[3] = (gw >> 24) & 0xFF;
            }
            if (st) {
                snprintf(st, 16, "%d.%d.%d.%d", gw & 0xFF, (gw >> 8) & 0xFF, (gw >> 16) & 0xFF, (gw >> 24) & 0xFF);
            }

            /* Subnet Mask */
            if (subnetmask) {
                subnetmask[0] = (mask & 0xFF);
                subnetmask[1] = (mask >> 8) & 0xFF;
                subnetmask[2] = (mask >> 16) & 0xFF;
                subnetmask[3] = (mask >> 24) & 0xFF;
            }
            if (stSubnetmask) {
                snprintf(stSubnetmask, 16, "%d.%d.%d.%d", mask & 0xFF, (mask >> 8) & 0xFF, (mask >> 16) & 0xFF, (mask >> 24) & 0xFF);
            }

            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return -2;
}

/**
 * @brief Resolve a domain name to an IPv4 address.
 * @param domain The domain name to resolve.
 * @param ip The resolved IPv4 address (in network byte order) - MUST BE 4 BYTES.
 * @param st The resolved IPv4 address in dotted decimal notation (e.g. "192.168.1.1") - MUST BE 16 BYTES.
 * @return 0 on success, -1 if getaddrinfo() fails, -2 if no IPv4 addresses are found.
 */
int Kiwi_DNS_ResolveDomain(const char *domain, uint8_t *ip, char *st) {
    int status;
    struct addrinfo hints, *res, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(domain, NULL, &hints, &res)) != 0) {
        printf("getaddrinfo() return %s\r\n", gai_strerror(status));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        if (ip) {
            memcpy(ip, &ipv4->sin_addr, 4);
        }
        if (st) {
            inet_ntop(AF_INET, &ipv4->sin_addr, st, 16);
        }
        freeaddrinfo(res);
        return 0;
    }

    freeaddrinfo(res);
    return -2;
}

/**
 * @brief Get the status of a given network interface.
 * @param interface The network interface name (e.g. "eth0").
 * @return The status of the network interface: IF_UP, IF_DOWN, IF_DORMANT, or IF_UNKNOWN.
 */
KIWI_NETWORK_NIC_STATUS Kiwi_NIC_GetStatus(const char *interface) {
    char cmds[64] = {0};
    char buffers[16] = {0};
	
    FILE *fp = NULL;
    int readBytes = 0;

	sprintf(cmds, "cat /sys/class/net/%s/operstate", interface);
	fp = popen(cmds, "r");
	if (fp) {
		readBytes = fread(buffers, sizeof(char), sizeof(buffers) - 1, fp);
		pclose(fp);
	}
    if (readBytes <= 0) {
        return KIWI_IF_UNKNOWN;
    }

    if (strncmp(buffers, "up", strlen("up")) == 0) {
        return KIWI_IF_UP;
    }
    else if (strncmp(buffers, "down", strlen("down")) == 0) {
        return KIWI_IF_DOWN;
    }
    else if (strncmp(buffers, "dormant", strlen("dormant")) == 0) {
        return KIWI_IF_DORMANT;
    }
    return KIWI_IF_UNKNOWN;
}

/**
 * @brief Ping a gateway with the given timeout.
 * @param gateway The IP address of the gateway to ping.
 * @param waitForSeconds The timeout in seconds to wait for a reply.
 * @return true if the ping was successful, false otherwise.
 */
bool Kiwi_ROUTE_Ping(const char *gateway, int waitForSeconds) {
    if (!gateway) {
        return false;
    }
    /* 
        -c 1 -> send 1 ping
        -W waitForSeconds -> wait timeout seconds for reply
    */
    char cmd[64] = {0};
    snprintf(cmd, sizeof(cmd), "ping -c 1 -W %d %s > /dev/null 2>&1", waitForSeconds, gateway);
    return (system(cmd) == 0) ? true : false;
}

/**
 * @brief Start a Wi-Fi scan.
 * @return 0 on success, -1 on failure.
 */
int Kiwi_WiFi_Scan() {
    return system("wpa_cli scan");
}

/**
 * @brief Check if a given SSID is already in the list.
 * @param list The list of SSIDs to check against.
 * @param count The number of SSIDs in the list.
 * @param ssid The SSID to check for.
 * @return true if the SSID is already in the list, false otherwise.
 */
static inline bool isDuplicateSSID(KIWI_NETWORK_WIFI_SCAN_S *list, int count, const char *ssid) {
    for (int i = 0; i < count; i++) {
        if (strcmp(list[i].ssid, ssid) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Get a list of Wi-Fi networks in range.
 * @param list Pointer to an array of KIWI_NETWORK_WIFI_SCAN_S structures.
 * @param len The number of elements in the list.
 * @return The number of Wi-Fi networks found (up to len).
 */
int Kiwi_WiFi_GetList(KIWI_NETWORK_WIFI_SCAN_S *list, int len) {
    int count = 0;
    char chars[8192] = {0};

    runCommand("wpa_cli scan_results", chars, sizeof(chars));
    /*
        Example results:
        bssid               frequency   signal level/flags              ssid
        00:11:22:33:44:55   2412        -40    [WPA2-PSK-CCMP][ESS]     HomeWiFi
    */
    char *line = strtok(chars, "\n");
    while (line && count < len) {
        if (strstr(line, "bssid") == NULL) {
            char *ptr = NULL;
            char *bssid = strtok_r(line, "\t", &ptr);
            char *freq = strtok_r(NULL, "\t", &ptr);
            char *signal = strtok_r(NULL, "\t", &ptr);
            char *flags = strtok_r(NULL, "\t", &ptr);
            char *ssid = strtok_r(NULL, "\t", &ptr);
            (void)freq;

            if (ssid && *ssid) {
                if (!isDuplicateSSID(list, count, ssid)) {
                    /* 
                        @SSID 
                    */
                    strncpy(list[count].ssid, ssid, sizeof(list[count].ssid) - 1);
                    /* 
                        @BSSID 
                    */
                    if (bssid) strncpy(list[count].bssid, bssid, sizeof(list[count].bssid) - 1);
                    /* 
                        @Signal strength 
                    */
                    list[count].strength = (signal) ? (signed char)atoi(signal) : 0;
                    /* 
                        @Security
                    */
                    if (flags) {
                        if (strstr(flags, "WPA3")) {
                            strncpy(list[count].security, "WPA3", sizeof(list[count].security) - 1);
                        } else if (strstr(flags, "WPA2")) {
                            strncpy(list[count].security, "WPA2", sizeof(list[count].security) - 1);
                        } else if (strstr(flags, "WPA")) {
                            strncpy(list[count].security, "WPA", sizeof(list[count].security) - 1);
                        } else if (strstr(flags, "WEP")) {
                            strncpy(list[count].security, "WEP", sizeof(list[count].security) - 1);
                        } else {
                            strncpy(list[count].security, "OPEN", sizeof(list[count].security) - 1);
                        }
                    } else {
                        strncpy(list[count].security, "UNKNOWN", sizeof(list[count].security) - 1);
                    }
                    count++;
                }
            }
        }
        line = strtok(NULL, "\n");
    }

    return count;
}

/**
 * @brief Check if the given Wi-Fi interface is connected to a network.
 * @param wirelessInterface The name of the Wi-Fi interface to check.
 * @return true if the interface is connected, false otherwise.
 */
bool Kiwi_WiFi_IsConnected(const char *wirelessInterface) {
    int fd = -1;
    struct iwreq wrq;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return 0;
    }

    memset(&wrq, 0, sizeof(wrq));
    strncpy(wrq.ifr_name, wirelessInterface, IFNAMSIZ - 1);
    /* Check if connected to an Access Point (AP) or NOT */
    if (ioctl(fd, SIOCGIWAP, &wrq) < 0) {
        close(fd);
        return false;
    }

    unsigned char *ap = (unsigned char *)wrq.u.ap_addr.sa_data;
    int connected = !(ap[0] == 0 && ap[1] == 0 && ap[2] == 0 && ap[3] == 0 && ap[4] == 0 && ap[5] == 0);
    if (!connected) {
        close(fd);
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, wirelessInterface, IFNAMSIZ - 1);
    /* Check if the interface has an IP address */
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        close(fd);
        return false;
    }
    close(fd);
    return true;
}

/**
 * @brief Get name of Wi-Fi interface is connected to a network.
 * @param wirelessInterface The name of the Wi-Fi interface to check.
 * @return true if the interface is connected, false otherwise.
 */
bool Kiwi_WiFi_GetSSIDConnected(const char *wirelessInterface, char *ssid, size_t ssidLen) {
    int fd;
    struct iwreq wrq;

    if (!ssid || ssidLen == 0) {
        return false;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    memset(&wrq, 0, sizeof(wrq));
    strncpy(wrq.ifr_name, wirelessInterface, IFNAMSIZ - 1);
    wrq.u.essid.pointer = ssid;
    wrq.u.essid.length = ssidLen;
    wrq.u.essid.flags = 1;
    if (ioctl(fd, SIOCGIWESSID, &wrq) < 0) {
        close(fd);
        return false;
    }
    ssid[wrq.u.essid.length] = '\0';

    close(fd);
    return true;
}

/**
 * @brief Start a Wi-Fi network access point with the given SSID and password.
 * @param ssid The SSID of the Wi-Fi network.
 * @param pssk The password of the Wi-Fi network.
 * @return 0 on success, -1 on failure.
 */
int Kiwi_WiFi_DoConnect(const char *ssid, const char *pssk) {
    int rc = -1;
    if (strlen(ssid) && strlen(pssk)) {
        rc = rk_wifi_connect_with_ssid(ssid, pssk);
    }
    else if (strlen(ssid)) {
        rc = rk_wifi_connect_with_ssid(ssid, NULL);
    }
    return rc;
}

/**
 * @brief Connect to a list of Wi-Fi networks.
 * @param list Pointer to an array of KIWI_NETWORK_WIFI_INFO_S structures.
 * @param len The number of elements in the list.
 * @return 0 on success, -1 on failure.
 *
 * This function updates the wpa_supplicant.conf file with the given list of Wi-Fi networks
 * and starts a Wi-Fi network connection process in the background.
 */
int Kiwi_WiFi_DoConnectList(KIWI_NETWORK_WIFI_INFO_S *list, int len) {
    printf("%s NOT SUPPORT\n", __func__);
    return -1;
}

/**
 * @brief Start a Wi-Fi network access point with the given SSID and password.
 * @param ssid The SSID of the Wi-Fi network.
 * @param pssk The password of the Wi-Fi network.
 * @return 0 on success, -1 on failure.
 */
int Kiwi_WiFi_RunHostapd(const char *ssid, const char *pssk) {
    char cmd[256] = {0};
    snprintf(cmd, sizeof(cmd), "rkwifi_server ap_cfg \"%s\" \"%s\"", ssid, pssk);
    return system(cmd);
}

/**
 * @brief Close the Wi-Fi interface.
 * @return 0 on success, -1 on failure.
 */
int Kiwi_WiFi_ForceClose(void) {
    return system("rkwifi_server ap_cfg_clr");
}

///////////////////////////////////////////////////////////////////////////////////////////
static char Kiwi_DNS_DomainName[32];
static pthread_t Kiwi_DNS_Thread = (pthread_t)NULL;

/**
 * @brief This function is called by pthread_create() to start a DNS server listening on port 53.
 * @param args NULL is expected.
 * @return NULL on success, otherwise the thread will exit with an error code.
 *
 * It will start a DNS server listening on port 53 and loop to receive incoming DNS queries.
 * For each query, it will check if the query name matches the domain name in Kiwi_DNS_DomainName.
 * If it does, it will build a DNS response packet and send it back to the client.
 */
static void *Kiwi_DNS_ServerListenCalls(void *args) {
    int fd = -1;
    struct sockaddr_in saddrin;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    saddrin.sin_family = AF_INET;
    saddrin.sin_addr.s_addr = INADDR_ANY;
    saddrin.sin_port = htons(53); /* Port default for DNS */
    if (bind(fd, (struct sockaddr *)&saddrin, sizeof(saddrin))) {
        perror("bind()");
        return NULL;
    }

    unsigned char ingoing[512];
    struct sockaddr_in caddrin;
    socklen_t sockLen = sizeof(caddrin);

    printf("Start Dns server on port 53\r\n");

    for (;;) {
        memset(ingoing, 0, sizeof(ingoing));
        int readBytes = recvfrom(fd, ingoing, sizeof(ingoing), 0, (struct sockaddr *)&caddrin, &sockLen);
        if (readBytes < 12) {
            continue;
        }

        printf("Dns ingoing total %d bytes\r\n", readBytes);

        /* Extract query name (convert to dotted string) */
        int q = 12; 
        int p = 0;
        char name[64] = {0};
        while (q < readBytes && ingoing[q] != 0 && p < sizeof(name) - 1) {
            int labelLen = ingoing[q++];
            if (labelLen + q > readBytes) {
                break;
            }
            for (int id = 0; id < labelLen; id++) {
                name[p++] = ingoing[q++];
            }
            if (ingoing[q] != 0) {
                name[p++] = '.';
            }
        }
        name[p] = '\0';

        /* Compare domain (case-insensitive) */
        if (strcasecmp(name, Kiwi_DNS_DomainName) != 0) {
            continue;
        }

        /* Build DNS response */
        ingoing[2] |= 0x80; /* Response flag */
        ingoing[3] |= 0x80; /* Recursion available */
        ingoing[7] = 1; /* 1 For answer */
        int pos = readBytes;
        ingoing[pos++] = 0xC0; ingoing[pos++] = 0x0C; /* Name pointer */
        ingoing[pos++] = 0x00; ingoing[pos++] = 0x01; /* Type A */
        ingoing[pos++] = 0x00; ingoing[pos++] = 0x01; /* Class IN */
        ingoing[pos++] = 0x00; ingoing[pos++] = 0x00; ingoing[pos++] = 0x00; ingoing[pos++] = 0x3C; /* TTL (60s) */
        ingoing[pos++] = 0x00; ingoing[pos++] = 0x04; /* Data length */
        ingoing[pos++] = 172; ingoing[pos++] = 14; ingoing[pos++] = 10; ingoing[pos++] = 1; /* IP 172.14.10.1 */
        sendto(fd, ingoing, pos, 0, (struct sockaddr *)&caddrin, sockLen);

        printf("Dns answers query for %s\n", name);
    }

    return NULL;
}

/**
 * @brief Start a DNS server with the given domain name.
 * @param domain The domain name to listen for.
 * @return 0 on success, -1 if the domain name is invalid.
 *
 * This function starts a DNS server in the background with the given domain name.
 * The DNS server listens on port 53 and responds to DNS queries for the given domain name.
 * The DNS server is single-threaded and does not handle concurrent requests.
 */
int Kiwi_DNS_StartServer(const char *domain) {
    if (!domain || strlen(domain) > sizeof(Kiwi_DNS_DomainName)) {
        return -1;
    }
    if (!Kiwi_DNS_Thread) {
        memset(Kiwi_DNS_DomainName, 0, sizeof(Kiwi_DNS_DomainName));
        strcpy(Kiwi_DNS_DomainName, domain);
        pthread_create(&Kiwi_DNS_Thread, NULL, Kiwi_DNS_ServerListenCalls, NULL);
    }
    return 0;
}

/**
 * @brief Close the DNS server.
 * @return 0 on success.
 *
 * This function cancels the DNS server thread and sets the thread ID to NULL.
 */
int Kiwi_DNS_CloseServer(void) {
    pthread_cancel(Kiwi_DNS_Thread);
    Kiwi_DNS_Thread = (pthread_t)NULL;
    return 0;
}