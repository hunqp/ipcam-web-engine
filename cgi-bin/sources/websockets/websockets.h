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
    void (*OnOpened)(int SocketFileDescriptor);
    void (*OnClosed)(int SocketFileDescriptor);
    void (*OnHandle)(int SocketFileDescriptor, const unsigned char *Message, uint64_t msgSize, int type);
} WebSocketEvents_t;

typedef struct WebSocketsContext_t *WebSocketsHandle_t;

extern WebSocketsHandle_t WebSocketsCreate(const char *Host, uint16_t Port, uint32_t TimeoutMs, uint8_t Total);
extern void WebSocketsSetEvents(WebSocketsHandle_t handle, const WebSocketEvents_t *events);
extern void WebSocketsDelete(WebSocketsHandle_t handle);

extern int WebSocketsSendText(int Client, const char *Message, uint64_t size);
extern int WebSocketsSendBinary(int Client, const char *Message, uint64_t size);
extern int WebSocketsCloseClient(int SocketFileDescriptor);
extern const char *WebSocketsGetPath(int SocketFileDescriptor);

#ifdef __cplusplus
}
#endif

#endif /* WEBSOCKETS_H */
