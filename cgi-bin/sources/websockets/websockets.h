#ifndef WEBSOCKETS_H
#define WEBSOCKETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#ifndef WS_MAX_CLIENTS
#define WS_MAX_CLIENTS              (5)
#endif

#define configMESSAGE_LENGTH       (2048)
#define configMAX_FRAME_LENGTH     (16 * 1024 * 1024)
#define configTHREAD_STACK_SIZE    (64 * 1024)
#define configKEY_LEN              (24)
#define configMS_LEN               (36)
#define configKEYMS_LEN            (configKEY_LEN + configMS_LEN)
#define configMAGIC_STRING         "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define configHANDSHAKE_REQ        "Sec-WebSocket-Key"
#define configHANDSHAKE_ACCLEN     (130)
#define configHANDSHAKE_ACCEPT              \
    "HTTP/1.1 101 Switching Protocols\r\n"  \
    "Upgrade: websocket\r\n"                \
    "Connection: Upgrade\r\n"               \
    "Sec-WebSocket-Accept: "

#define configFIN                  (128)
#define configFIN_SHIFT            (7)
#define configFR_OP_CONT           (0)
#define configFR_OP_TXT            (1)
#define configFR_OP_BIN            (2)
#define configFR_OP_CLSE           (8)
#define configFR_OP_PING           (0x9)
#define configFR_OP_PONG           (0xA)

#define configCLSE_NORMAL          (1000)
#define configCLSE_PROTERR         (1002)
#define configCLSE_BIGMSG          (1009)

#define configSTATE_CONNECTING     (0)
#define configSTATE_OPEN           (1)
#define configSTATE_CLOSING        (2)
#define configSTATE_CLOSED         (3)

#define WS_MS_TO_NS(x)              ((x) * 1000000)
#define WS_TIMEOUT_MS               (2000)

typedef struct {
    void (*OnOpened)(int sfd);
    void (*OnClosed)(int sfd);
    void (*OnHandle)(int sfd, const unsigned char *Message, uint64_t msgSize, int type);
    /**
     * Called during the HTTP upgrade, AFTER the request line/headers have been
     * received but BEFORE the "101 Switching Protocols" reply is sent. It is
     * handed the raw, NUL-terminated handshake buffer so the host can inspect
     * the Cookie header / query string and decide whether the peer is allowed.
     * Return 0 to accept the connection, non-zero to reject it (the server then
     * answers "401 Unauthorized" and drops the socket without firing OnOpened).
     * When NULL, every well-formed handshake is accepted (legacy behaviour).
     */
    int (*OnAuthorise)(int sfd, const char *RawHandshake);
} lw_wss_events_t;

typedef struct lw_wss_t *lw_wss_handle_t;

extern lw_wss_handle_t lw_wss_create(const char *Host, uint16_t Port, uint32_t TimeoutMs, uint8_t Total);
extern void lw_wss_set_events(lw_wss_handle_t handle, const lw_wss_events_t *events);
extern void lw_wss_delete(lw_wss_handle_t handle);

extern int lw_wss_send_str(int Client, const char *Message, uint64_t size);
extern int lw_wss_send_bin(int Client, const char *Message, uint64_t size);
extern int lw_wss_close_peer(int sfd);
extern const char *lw_wss_get_path(int sfd);

#ifdef __cplusplus
}
#endif

#endif /* WEBSOCKETS_H */
