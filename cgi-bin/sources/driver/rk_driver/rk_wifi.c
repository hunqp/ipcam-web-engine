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

#include "rk_wifi.h"

int rk_wiFi_start_ap(const char *interface, const char *ssid, const char *pssk) {
    if (!ssid || !pssk) {
        return -1;
    }

    const char *DNSMASQ_CONFIG_FILE = (const char*)"/tmp/dnsmasq.conf";
    const char *HOSTAPD_CONFIG_FILE = (const char*)"/tmp/hostapd.conf";

    printf("[NETWORK-AP] SSID: %s, PSSK: %s\n", ssid, pssk);

    /* Stop all services that may interfere */
    system("killall udhcpc");
    system("killall dnsmasq");
    system("killall hostapd");
    system("killall wpa_supplicant");

    /* Create HOSTAPD_CONFIG_FILE */
    FILE *fp = fopen(HOSTAPD_CONFIG_FILE, "w");
    if (fp) {
        fprintf(fp,
            "interface=%s\n"
            "ctrl_interface=/var/run/hostapd\n"
            "driver=nl80211\n"
            "ssid=%s\n"
            "wpa_passphrase=%s\n"
            "channel=6\n"
            "hw_mode=g\n"
            "ieee80211n=1\n"
            "wpa=3\n"
            "ignore_broadcast_ssid=0\n"
            "auth_algs=1\n"
            "wpa_key_mgmt=WPA-PSK\n"
            "rsn_pairwise=CCMP\n"
            "wpa_pairwise=CCMP\n",
            interface, ssid, pssk);
        fclose(fp);   
    }

    /* Create DNSMASQ_CONFIG_FILE */
    fp = fopen(DNSMASQ_CONFIG_FILE, "w");
    if (fp) {
        fprintf(fp,
            "user=root\n"
            "listen-address=172.14.10.1\n"
            "dhcp-range=172.14.10.50,172.14.10.150\n"
            "server=/setup.vivoo.vn/34.98.122.10\n");
        fclose(fp);
    }

    /* Start hostapd */
    char cmds[128];
    memset(cmds, 0, sizeof(cmds));
    snprintf(cmds, sizeof(cmds), "ifconfig %s up", interface);
    system(cmds);

    memset(cmds, 0, sizeof(cmds));
    snprintf(cmds, sizeof(cmds), "ifconfig %s 172.14.10.1 netmask 255.255.255.0", interface);
    system(cmds);

    // memset(cmds, 0, sizeof(cmds));
    // snprintf(cmds, sizeof(cmds), "route add default gw 172.14.10.1 %s", interface);
    // system(cmds);

    memset(cmds, 0, sizeof(cmds));
    snprintf(cmds, sizeof(cmds), "dnsmasq -C %s --interface=%s", DNSMASQ_CONFIG_FILE, interface);
    system(cmds);
    memset(cmds, 0, sizeof(cmds));
    snprintf(cmds, sizeof(cmds), "hostapd %s &", HOSTAPD_CONFIG_FILE);
    system(cmds);

    int time = 100;
	while (time-- > 0 && access("/var/run/hostapd", F_OK)) {
		usleep(100 * 1000);
	}
    return 0;
}

int rk_wiFi_start_sta(void) {
    return system("rkwifi_server start > /dev/null &");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
static char dnsDomainName[32];
static pthread_t tDnsSerId = (pthread_t)NULL;

static void *serDnsListenCalls(void *args) {
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
        if (strcasecmp(name, dnsDomainName) != 0) {
            continue;
        }

        /* Build DNS response */
        ingoing[2] |= 0x80;        // response flag
        ingoing[3] |= 0x80;        // recursion available
        ingoing[7] = 1;            // 1 answer
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

int rk_wiFi_start_dns(const char *domain) {
    if (!domain || strlen(domain) > sizeof(dnsDomainName)) {
        return -1;
    }
    if (!tDnsSerId) {
        memset(dnsDomainName, 0, sizeof(dnsDomainName));
        strcpy(dnsDomainName, domain);
        pthread_create(&tDnsSerId, NULL, serDnsListenCalls, NULL);
    }
    return 0;
}

int rk_wiFi_close_dns(void) {
    pthread_cancel(tDnsSerId);
    tDnsSerId = (pthread_t)NULL;
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
int rk_wiFi_get_mac(const char *interface, uint8_t mac[6]) {
    char chars[32] = { 0 };
    snprintf(chars, sizeof(chars), (const char *)"/sys/class/net/%s/address", interface);

    FILE *fp = fopen(chars, "r");
    if (fp) {
        memset(chars, 0, sizeof(chars));
        while (fgets(chars, sizeof(chars), fp));
        fclose(fp);
        unsigned int values[6];
        if (sscanf(chars, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) != 6) {
            return -1;
        }

        for (int i = 0; i < 6; ++i) {
            mac[i] = (uint8_t)values[i];
        }

        return 0;
    }

    return -2;
}

int rk_wiFi_get_ip4(const char *interface, uint8_t ip4[4]) {
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
            if (ip4) {
                memcpy(ip4, &sa->sin_addr, 4);
            }
            boolean = true;
        }
    }

    freeifaddrs(list);

    return (boolean ? 0 : -2);
}