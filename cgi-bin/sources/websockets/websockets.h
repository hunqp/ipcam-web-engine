/*
 * Copyright (C) 2016-2023  Davidson Francis <davidsondfgl@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#ifndef WEBSOCKETS_H
#define WEBSOCKETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

/*-----------------------------------------------------------
 * Server limits / tunables.
 *----------------------------------------------------------*/

/* The maximum number of peers that may be connected to a single server
 * instance at once ( the size of its fixed peer pool ). */
#ifndef wsMAX_CLIENTS
    #define wsMAX_CLIENTS               ( 5 )
#endif

/* The size, in bytes, of the per-peer socket read scratch buffer. */
#define wsMESSAGE_LENGTH                ( 2048 )

/* The hard ceiling, in bytes, on a fully reassembled message ( data or
 * control ), enforced while frames are still being read off the wire. */
#define wsMAX_FRAME_LENGTH              ( 16 * 1024 * 1024 )

/* The stack size, in bytes, reserved for every worker thread created by
 * this module ( the accept thread and each per-peer thread ). */
#define wsTHREAD_STACK_SIZE             ( 64 * 1024 )

/*-----------------------------------------------------------
 * RFC 6455 handshake constants.
 *----------------------------------------------------------*/

/* The length, in bytes, of a base64-encoded "Sec-WebSocket-Key" value. */
#define wsKEY_LEN                       ( 24 )

/* The length, in bytes, of the RFC 6455 GUID appended to the key before
 * hashing. */
#define wsMAGIC_STRING_LEN              ( 36 )

/* The combined length of the key and the GUID, i.e. what actually gets
 * fed into SHA-1 to produce "Sec-WebSocket-Accept". */
#define wsKEY_MAGIC_LEN                 ( wsKEY_LEN + wsMAGIC_STRING_LEN )

/* The RFC 6455 well-known GUID used when computing the handshake accept
 * hash. This value is fixed by the specification and must not change. */
#define wsMAGIC_STRING                  "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* The request header carrying the client's handshake key. */
#define wsHANDSHAKE_KEY_HEADER          "Sec-WebSocket-Key"

/* The allocation size for the formatted "101 Switching Protocols"
 * response string ( fixed header text + base64 accept hash + CRLFs ). */
#define wsHANDSHAKE_ACCEPT_LEN          ( 130 )

/* The fixed header block written ahead of the "Sec-WebSocket-Accept"
 * value on a successful handshake. */
#define wsHANDSHAKE_ACCEPT_HEADER                  \
    "HTTP/1.1 101 Switching Protocols\r\n"        \
    "Upgrade: websocket\r\n"                      \
    "Connection: Upgrade\r\n"                     \
    "Sec-WebSocket-Accept: "

/*-----------------------------------------------------------
 * RFC 6455 frame header bits.
 *----------------------------------------------------------*/

/* The FIN bit ( bit 7 ) of the first frame header byte. */
#define wsFIN_BIT                       ( 128 )

/* The bit offset of the FIN flag inside the first frame header byte. */
#define wsFIN_SHIFT                     ( 7 )

/* Opcode: continuation of a fragmented message. */
#define wsOPCODE_CONTINUATION           ( 0 )
/* Opcode: UTF-8 text frame. */
#define wsOPCODE_TEXT                   ( 1 )
/* Opcode: binary frame. */
#define wsOPCODE_BINARY                 ( 2 )
/* Opcode: connection close. */
#define wsOPCODE_CLOSE                  ( 8 )
/* Opcode: ping ( keep-alive probe ). */
#define wsOPCODE_PING                   ( 0x9 )
/* Opcode: pong ( keep-alive reply ). */
#define wsOPCODE_PONG                   ( 0xA )

/*-----------------------------------------------------------
 * RFC 6455 close status codes.
 *----------------------------------------------------------*/

/* Normal, expected closure. */
#define wsCLOSE_NORMAL                  ( 1000 )
/* The endpoint is terminating the connection due to a protocol error. */
#define wsCLOSE_PROTOCOL_ERROR          ( 1002 )
/* The frame payload data is not valid ( e.g. malformed UTF-8 text ). */
#define wsCLOSE_INVALID_PAYLOAD         ( 1007 )
/* The message exceeds wsMAX_FRAME_LENGTH and was rejected. */
#define wsCLOSE_MESSAGE_TOO_BIG         ( 1009 )

/*-----------------------------------------------------------
 * Connection lifecycle states.
 *----------------------------------------------------------*/

/* The peer slot has been accepted but the HTTP upgrade handshake has not
 * completed yet. */
#define wsSTATE_CONNECTING              ( 0 )
/* The handshake is complete; frames may be exchanged in both directions. */
#define wsSTATE_OPEN                    ( 1 )
/* A close frame has been sent or received; waiting for the peer to
 * finish its side of the close handshake. */
#define wsSTATE_CLOSING                 ( 2 )
/* The socket has been shut down and the slot is free for reuse. */
#define wsSTATE_CLOSED                  ( 3 )

/*-----------------------------------------------------------
 * Timeouts.
 *----------------------------------------------------------*/

/* Converts a millisecond count into nanoseconds, for use with
 * struct timespec ( e.g. clock_gettime() deadlines ). */
#define wsMS_TO_NS( xMilliseconds )     ( ( xMilliseconds ) * 1000000 )

/* The grace period, in milliseconds, given to a peer to finish its own
 * close handshake before the server force-closes the socket. */
#define wsTIMEOUT_MS                    ( 2000 )

/**
 * WebSocketEvents_t
 *
 * Host-supplied event callbacks, installed once via vWebSocketSetEvents().
 * Every callback runs on the worker thread that owns the connection
 * ( pxOnAuthorise runs on that same thread, during the handshake, before
 * the "101 Switching Protocols" reply is sent ). Implementations must not
 * block for long: a slow callback only stalls the one connection it was
 * called for, but a slow pxOnAuthorise stalls that connection's handshake.
 */
typedef struct WebSocketEvents
{
    /* Fired right after the connection reaches wsSTATE_OPEN. */
    void ( * pxOnOpened )( int xSocketFd );

    /* Fired once the connection has been fully torn down. */
    void ( * pxOnClosed )( int xSocketFd );

    /* Fired for every complete text/binary message received. */
    void ( * pxOnHandle )( int xSocketFd, const unsigned char * pucMessage, uint64_t ullMessageSize, int lFrameType );

    /*
     * Called during the HTTP upgrade, AFTER the request line/headers have
     * been received but BEFORE the "101 Switching Protocols" reply is
     * sent. It is handed the raw, NUL-terminated handshake buffer so the
     * host can inspect the Cookie header / query string and decide
     * whether the peer is allowed. Return 0 to accept the connection,
     * non-zero to reject it ( the server then answers "401 Unauthorized"
     * and drops the socket without firing pxOnOpened ). When NULL, every
     * well-formed handshake is accepted ( legacy behaviour ).
     */
    int ( * pxOnAuthorise )( int xSocketFd, const char * pcRawHandshake );
} WebSocketEvents_t;

/* Opaque handle to a running WebSocket server instance, returned by
 * xWebSocketCreate() and consumed by every other public API call. */
typedef struct WebSocketServer * WebSocketHandle_t;

/**
 * xWebSocketCreate
 *
 * Creates and starts a WebSocket server bound to pcHost:usPort.
 *
 * @param pcHost      Address to bind ( NULL/"" lets the OS pick, per
 *                     getaddrinfo() ).
 * @param usPort      TCP port to listen on.
 * @param ulTimeoutMs Per-socket SO_SNDTIMEO, in milliseconds ( 0 disables
 *                     the send timeout ).
 * @param ucTotalSlots Maximum number of concurrent peers ( sizes the
 *                     wsMAX_CLIENTS-style peer pool ).
 *
 * @return A valid handle on success, or NULL on failure ( bind, listen or
 *         allocation error ).
 */
extern WebSocketHandle_t xWebSocketCreate( const char * pcHost, uint16_t usPort, uint32_t ulTimeoutMs, uint8_t ucTotalSlots );

/**
 * vWebSocketSetEvents
 *
 * Installs, or replaces, the event callback table of a running server.
 */
extern void vWebSocketSetEvents( WebSocketHandle_t xServer, const WebSocketEvents_t * pxEvents );

/**
 * vWebSocketDelete
 *
 * Stops the server, joins its threads and releases every resource that
 * xWebSocketCreate() allocated for it.
 */
extern void vWebSocketDelete( WebSocketHandle_t xServer );

/**
 * xWebSocketSendText
 *
 * Sends a UTF-8 text frame to the peer identified by xSocketFd.
 *
 * @return The number of bytes queued on success, or -1 on failure.
 */
extern int xWebSocketSendText( int xSocketFd, const char * pcMessage, uint64_t ullSize );

/**
 * xWebSocketSendBinary
 *
 * Sends a binary frame to the peer identified by xSocketFd.
 *
 * @return The number of bytes queued on success, or -1 on failure.
 */
extern int xWebSocketSendBinary( int xSocketFd, const char * pcMessage, uint64_t ullSize );

/**
 * xWebSocketClosePeer
 *
 * Starts a graceful close handshake for the given peer.
 *
 * @return 0 on success, -1 otherwise.
 */
extern int xWebSocketClosePeer( int xSocketFd );

/**
 * pcWebSocketGetPath
 *
 * @return The HTTP request path captured at handshake time, or NULL if
 *         xSocketFd does not name a currently-known peer.
 */
extern const char * pcWebSocketGetPath( int xSocketFd );

#ifdef __cplusplus
}
#endif

#endif /* WEBSOCKETS_H */
