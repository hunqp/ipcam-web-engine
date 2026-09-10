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
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include <netdb.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0
#endif

#include "utf8.h"
#include "websockets.h"

/* Queues ulLen bytes to pxPeer, blocking until fully sent or an error
 * occurs. */
#define wsSEND( pxPeer, pvBuf, xLen )    prvSendAll( ( pxPeer ), ( pvBuf ), ( xLen ), MSG_NOSIGNAL )

/* Reads whatever is currently available from pxPeer into pvBuf ( a single
 * recv() call ). */
#define wsRECV( pxPeer, pvBuf, xLen )    recv( ( pxPeer )->xSocket, ( pvBuf ), ( xLen ), 0 )

/*
 * wsCLIENT_IS_VALID
 *
 * Sanity check for a WebSocketPeer_t pointer. Guards every public-facing
 * lookup against a stale/foreign pointer: the peer must belong to the
 * slot pool of the server it claims ( pxServer ) and must currently hold
 * an open socket. Cheap enough ( pointer-range arithmetic ) to call on
 * every hot-path access.
 */
#define wsCLIENT_IS_VALID( pxPeer )                                            \
    ( ( pxPeer ) != NULL && ( pxPeer )->pxServer != NULL &&                   \
      ( pxPeer ) >= &( pxPeer )->pxServer->pxClients[ 0 ] &&                 \
      ( pxPeer ) < &( pxPeer )->pxServer->pxClients[ ( pxPeer )->pxServer->ucTotalPeers ] && \
      ( pxPeer )->xSocket > -1 )

/* Implemented in handshake.c: tokenises the raw HTTP request in place and
 * builds the "101 Switching Protocols" response ( caller must free() the
 * response string ). */
extern int xGetHandshakeResponse( char * pcRequest, char ** ppcResponse );

typedef struct WebSocketPeer WebSocketPeer_t;

/* WebSocketServer_t: a running WebSocket server, its listening socket and
 * its fixed-size peer pool. */
struct WebSocketServer
{
    const char * pcHost;                 /* Bind address passed at creation.            */
    uint16_t usPort;                     /* Bind port passed at creation.                */
    uint32_t ulTimeoutMs;                /* Per-socket SO_SNDTIMEO, 0 = disabled.        */
    uint8_t ucTotalPeers;                /* Capacity of the pxClients slot pool.         */
    WebSocketEvents_t xEvents;           /* Host callback table.                         */
    bool xRunning;                       /* Cleared by vWebSocketDelete() to stop.       */
    pthread_t xThread;                   /* Accept-loop thread.                          */
    int xSocket;                         /* Listening socket fd.                         */
    pthread_mutex_t xClientsMutex;       /* Guards slot allocation in pxClients.         */
    WebSocketPeer_t * pxClients;         /* Fixed-size pool of peer slots.               */
    struct WebSocketServer * pxNext;     /* Intrusive link in the global server list.    */
};

/* WebSocketPeer_t: one connected peer / slot in a server's pool. */
struct WebSocketPeer
{
    int xSocket;                         /* Socket fd, or -1 when the slot is free.      */
    int xState;                          /* One of the wsSTATE_* values.                 */
    struct WebSocketServer * pxServer;   /* Owning server ( also the pool's base ptr ).  */
    pthread_mutex_t xStateMutex;         /* Guards xState.                               */
    pthread_cond_t xCloseCond;           /* Signalled when xState becomes wsSTATE_CLOSED.*/
    pthread_t xTimeoutThread;            /* Watchdog started by a graceful close.        */
    bool xCloseThreadActive;             /* True while xTimeoutThread is alive.          */
    pthread_mutex_t xSendMutex;          /* Serialises writes to xSocket.                */
    int32_t lLastPongId;                 /* Highest ping id acknowledged so far.         */
    int32_t lCurrentPingId;              /* Highest ping id sent so far.                 */
    pthread_mutex_t xPingMutex;          /* Guards lLastPongId / lCurrentPingId.         */
    int lClientId;                       /* Public id handed out to the host.            */
    char pcPath[ 128 ];                  /* HTTP request path captured at handshake.     */
};

/*
 * WebSocketFrame_t
 *
 * Per-connection frame reassembly context. Lives on the worker thread's
 * stack for the whole lifetime of the peer; pucFrameBuffer is the raw
 * recv() scratch space and pucMessage is the heap-allocated, unmasked
 * payload handed to the host once a frame ( or fragmented message ) is
 * complete.
 */
typedef struct
{
    unsigned char pucFrameBuffer[ wsMESSAGE_LENGTH ]; /* Raw recv() scratch space.        */
    unsigned char * pucMessage;                       /* Reassembled, unmasked payload.   */
    unsigned char pucControlMessage[ 125 ];           /* Payload of the current control frame. */
    size_t xCurrentPosition;                          /* Read cursor inside pucFrameBuffer. */
    size_t xBytesRead;                                /* Valid byte count in pucFrameBuffer. */
    int lFrameType;                                   /* Opcode of the message being assembled. */
    uint64_t ullFrameSize;                            /* Total bytes assembled into pucMessage. */
    int xError;                                       /* Non-zero on any protocol/IO violation. */
    WebSocketPeer_t * pxPeer;                         /* Peer this context belongs to.    */
} WebSocketFrame_t;

/* WebSocketFrameState_t: scratch state threaded through a single call to
 * prvGetNextCompleteFrame(). */
typedef struct
{
    unsigned char * pucMessageData;      /* Growing buffer for data frames.              */
    unsigned char * pucControlMessage;   /* Alias of pxFrame->pucControlMessage.         */
    uint8_t ucDataMasks[ 4 ];            /* Masking key of the current data frame.       */
    uint8_t ucControlMasks[ 4 ];         /* Masking key of the current control frame.    */
    uint64_t ullDataMessageIndex;        /* Write cursor inside pucMessageData.          */
    uint64_t ullControlMessageIndex;     /* Write cursor inside pucControlMessage.       */
    uint64_t ullFrameLength;             /* Payload length of the frame in progress.     */
    uint64_t ullFrameSize;               /* Payload length of a control frame.           */
    #ifdef VALIDATE_UTF8
        uint32_t ulUtf8State;            /* Running UTF-8 decoder state.                 */
    #endif
    int32_t lPongId;                     /* Ping id decoded from a PONG payload.         */
    uint8_t ucOpcode;                    /* Opcode of the frame currently parsed.        */
    uint8_t ucIsFin;                     /* FIN bit of the frame currently parsed.       */
    uint8_t ucMask;                      /* Raw mask/length byte of the frame.           */
    int lCurrentByte;                    /* Last byte pulled off the wire.               */
} WebSocketFrameState_t;

/* The next public client id to hand out from prvAcceptTask(); 0 is never
 * used, so callers can treat it as "no id". */
static uint32_t ulNextClientId = 1;

/* Head of the intrusive list of every server created via
 * xWebSocketCreate(). */
static WebSocketHandle_t pxServerList = NULL;

/* Protects pxServerList and cross-server peer lookups. */
static pthread_mutex_t xGeneralMutex = PTHREAD_MUTEX_INITIALIZER;

/*-----------------------------------------------------------*/

/*
 * prvFindPeerById
 *
 * Looks up the peer with public id xSocketFd across every server.
 * O( servers * ucTotalPeers ) worst case, but ucTotalPeers is small
 * ( wsMAX_CLIENTS ) and lookups are not on the per-byte hot path, so a
 * linear scan under the two guarding mutexes is simpler and safer here
 * than a hash table.
 */
static WebSocketPeer_t * prvFindPeerById( int xSocketFd )
{
    WebSocketPeer_t * pxFound = NULL;
    WebSocketHandle_t pxServer;
    uint8_t ucIndex;

    pthread_mutex_lock( &xGeneralMutex );
    for( pxServer = pxServerList; pxServer != NULL; pxServer = pxServer->pxNext )
    {
        pthread_mutex_lock( &pxServer->xClientsMutex );
        for( ucIndex = 0; ucIndex < pxServer->ucTotalPeers; ++ucIndex )
        {
            if( pxServer->pxClients[ ucIndex ].lClientId == xSocketFd )
            {
                pxFound = &pxServer->pxClients[ ucIndex ];
                break;
            }
        }
        pthread_mutex_unlock( &pxServer->xClientsMutex );

        if( pxFound != NULL )
        {
            break;
        }
    }
    pthread_mutex_unlock( &xGeneralMutex );

    return pxFound;
}
/*-----------------------------------------------------------*/

/* prvCloseSocket: shuts down and closes a raw socket fd; a no-op for an
 * already-invalid fd. */
static void prvCloseSocket( int xFd )
{
    if( xFd >= 0 )
    {
        shutdown( xFd, SHUT_RDWR );
        close( xFd );
    }
}
/*-----------------------------------------------------------*/

/* prvSaveRequestPath: extracts the request-line path
 * ( "GET <path> HTTP/1.1" ) into pxPeer->pcPath. */
static void prvSaveRequestPath( WebSocketPeer_t * pxPeer, const char * pcRequest )
{
    const char * pcMethodEnd = strchr( pcRequest, ' ' );
    const char * pcPathStart;
    const char * pcPathEnd;
    size_t xLen;

    if( pcMethodEnd == NULL )
    {
        pxPeer->pcPath[ 0 ] = '\0';
        return;
    }

    pcPathStart = pcMethodEnd + 1;
    pcPathEnd = strchr( pcPathStart, ' ' );

    if( ( pcPathEnd == NULL ) || ( pcPathEnd <= pcPathStart ) )
    {
        pxPeer->pcPath[ 0 ] = '\0';
        return;
    }

    xLen = ( size_t ) ( pcPathEnd - pcPathStart );

    if( xLen >= sizeof( pxPeer->pcPath ) )
    {
        xLen = sizeof( pxPeer->pcPath ) - 1;
    }

    memcpy( pxPeer->pcPath, pcPathStart, xLen );
    pxPeer->pcPath[ xLen ] = '\0';
}
/*-----------------------------------------------------------*/

/*
 * prvCreateThread
 *
 * Spawns a thread with wsTHREAD_STACK_SIZE instead of the platform
 * default, falling back to default-attribute creation if the stack size
 * cannot be configured.
 */
static int prvCreateThread( pthread_t * pxThread, void * ( *pxStart )( void * ), void * pvArg )
{
    pthread_attr_t xAttr;
    size_t xStackSize = wsTHREAD_STACK_SIZE;
    int lReturn = pthread_attr_init( &xAttr );

    if( lReturn != 0 )
    {
        return pthread_create( pxThread, NULL, pxStart, pvArg );
    }

    #ifdef PTHREAD_STACK_MIN
        if( xStackSize < PTHREAD_STACK_MIN )
        {
            xStackSize = PTHREAD_STACK_MIN;
        }
    #endif

    lReturn = pthread_attr_setstacksize( &xAttr, xStackSize );

    if( lReturn != 0 )
    {
        pthread_attr_destroy( &xAttr );
        return pthread_create( pxThread, NULL, pxStart, pvArg );
    }

    lReturn = pthread_create( pxThread, &xAttr, pxStart, pvArg );
    pthread_attr_destroy( &xAttr );
    return lReturn;
}
/*-----------------------------------------------------------*/

/* prvGetPeerState: thread-safe read of pxPeer->xState. Returns -1 if
 * pxPeer is not a valid slot. */
static int prvGetPeerState( WebSocketPeer_t * pxPeer )
{
    int lState;

    if( !wsCLIENT_IS_VALID( pxPeer ) )
    {
        return -1;
    }

    pthread_mutex_lock( &pxPeer->xStateMutex );
    lState = pxPeer->xState;
    pthread_mutex_unlock( &pxPeer->xStateMutex );
    return lState;
}
/*-----------------------------------------------------------*/

/* prvSetPeerState: thread-safe write of pxPeer->xState. Returns -1 on an
 * invalid peer or an out-of-range state. */
static int prvSetPeerState( WebSocketPeer_t * pxPeer, int lState )
{
    if( !wsCLIENT_IS_VALID( pxPeer ) )
    {
        return -1;
    }

    if( ( lState < wsSTATE_CONNECTING ) || ( lState > wsSTATE_CLOSED ) )
    {
        return -1;
    }

    pthread_mutex_lock( &pxPeer->xStateMutex );
    pxPeer->xState = lState;
    pthread_mutex_unlock( &pxPeer->xStateMutex );
    return 0;
}
/*-----------------------------------------------------------*/

/*
 * prvSendAll
 *
 * Writes exactly xLen bytes to pxPeer, looping over short writes.
 * Serialised by pxPeer->xSendMutex so frames queued from different
 * threads ( e.g. the host pushing data while a ping/pong runs ) never
 * interleave on the wire.
 */
static ssize_t prvSendAll( WebSocketPeer_t * pxPeer, const void * pvBuf, size_t xLen, int lFlags )
{
    const char * pcCursor;
    ssize_t xSent;
    ssize_t xChunk;

    xSent = 0;

    if( !wsCLIENT_IS_VALID( pxPeer ) )
    {
        return -1;
    }

    pcCursor = pvBuf;
    pthread_mutex_lock( &pxPeer->xSendMutex );

    while( xLen != 0U )
    {
        xChunk = send( pxPeer->xSocket, pcCursor, xLen, lFlags );

        if( xChunk == -1 )
        {
            pthread_mutex_unlock( &pxPeer->xSendMutex );
            return -1;
        }

        pcCursor += xChunk;
        xLen -= ( size_t ) xChunk;
        xSent += xChunk;
    }

    pthread_mutex_unlock( &pxPeer->xSendMutex );
    return xSent;
}
/*-----------------------------------------------------------*/

/*
 * prvSendAllIoV
 *
 * Writes a WebSocket frame ( header + payload ) in a single syscall.
 * Using sendmsg()/iovec instead of two separate send() calls avoids a
 * copy to stitch header and payload together, and avoids Nagle-related
 * latency from issuing them as two TCP segments - this is the main
 * throughput-sensitive path for outgoing video/data frames.
 */
static ssize_t prvSendAllIoV( WebSocketPeer_t * pxPeer, const struct iovec * pxIoVec, int lIoVecCount, int lFlags )
{
    struct iovec xVec[ 2 ];
    struct msghdr xMsg;
    ssize_t xSent = 0;
    ssize_t xChunk;

    if( !wsCLIENT_IS_VALID( pxPeer ) || ( lIoVecCount < 1 ) || ( lIoVecCount > 2 ) )
    {
        return -1;
    }

    memcpy( xVec, pxIoVec, sizeof( *pxIoVec ) * ( size_t ) lIoVecCount );
    memset( &xMsg, 0, sizeof( xMsg ) );
    xMsg.msg_iov = xVec;
    xMsg.msg_iovlen = ( size_t ) lIoVecCount;

    pthread_mutex_lock( &pxPeer->xSendMutex );

    while( xMsg.msg_iovlen > 0U )
    {
        xChunk = sendmsg( pxPeer->xSocket, &xMsg, lFlags );

        if( xChunk <= 0 )
        {
            pthread_mutex_unlock( &pxPeer->xSendMutex );
            return -1;
        }

        xSent += xChunk;

        while( ( xMsg.msg_iovlen > 0U ) && ( ( size_t ) xChunk >= xMsg.msg_iov[ 0 ].iov_len ) )
        {
            xChunk -= ( ssize_t ) xMsg.msg_iov[ 0 ].iov_len;
            xMsg.msg_iov++;
            xMsg.msg_iovlen--;
        }

        if( ( xMsg.msg_iovlen > 0U ) && ( xChunk > 0 ) )
        {
            xMsg.msg_iov[ 0 ].iov_base = ( char * ) xMsg.msg_iov[ 0 ].iov_base + xChunk;
            xMsg.msg_iov[ 0 ].iov_len -= ( size_t ) xChunk;
        }
    }

    pthread_mutex_unlock( &pxPeer->xSendMutex );
    return xSent;
}
/*-----------------------------------------------------------*/

/*
 * prvClosePeer
 *
 * Tears a peer down: closes the socket and destroys its sync objects.
 *
 * @param pxPeer     Peer to close.
 * @param xLockPool  When non-zero, pxPeer->pxServer->xClientsMutex is
 *                    held while clearing xSocket, so a concurrent
 *                    prvAcceptTask() scan for a free slot cannot race
 *                    with this reset.
 */
static void prvClosePeer( WebSocketPeer_t * pxPeer, int xLockPool )
{
    if( !wsCLIENT_IS_VALID( pxPeer ) )
    {
        return;
    }

    prvSetPeerState( pxPeer, wsSTATE_CLOSED );
    prvCloseSocket( pxPeer->xSocket );

    if( xLockPool != 0 )
    {
        pthread_mutex_lock( &pxPeer->pxServer->xClientsMutex );
    }

    pxPeer->xSocket = -1;
    pthread_cond_destroy( &pxPeer->xCloseCond );
    pthread_mutex_destroy( &pxPeer->xStateMutex );
    pthread_mutex_destroy( &pxPeer->xSendMutex );
    pthread_mutex_destroy( &pxPeer->xPingMutex );

    if( xLockPool != 0 )
    {
        pthread_mutex_unlock( &pxPeer->pxServer->xClientsMutex );
    }
}
/*-----------------------------------------------------------*/

/*
 * prvCloseTimeoutTask
 *
 * Watchdog thread: force-closes a peer if it never finishes the close
 * handshake within wsTIMEOUT_MS of entering wsSTATE_CLOSING.
 */
static void * prvCloseTimeoutTask( void * pvArg )
{
    WebSocketPeer_t * pxPeer = pvArg;
    struct timespec xDeadline;
    int lState;

    pthread_mutex_lock( &pxPeer->xStateMutex );

    clock_gettime( CLOCK_REALTIME, &xDeadline );
    xDeadline.tv_nsec += wsMS_TO_NS( wsTIMEOUT_MS );

    while( xDeadline.tv_nsec >= 1000000000 )
    {
        xDeadline.tv_sec++;
        xDeadline.tv_nsec -= 1000000000;
    }

    while( ( pxPeer->xState != wsSTATE_CLOSED ) &&
           ( pthread_cond_timedwait( &pxPeer->xCloseCond, &pxPeer->xStateMutex, &xDeadline ) != ETIMEDOUT ) )
    {
        /* Keep waiting until the peer closes or the deadline passes. */
    }

    lState = pxPeer->xState;
    pthread_mutex_unlock( &pxPeer->xStateMutex );

    if( lState != wsSTATE_CLOSED )
    {
        prvClosePeer( pxPeer, 1 );
    }

    return NULL;
}
/*-----------------------------------------------------------*/

/* prvStartCloseTimeout: moves a peer into wsSTATE_CLOSING and arms its
 * close watchdog. */
static int prvStartCloseTimeout( WebSocketPeer_t * pxPeer )
{
    int lReturn = 0;

    if( !wsCLIENT_IS_VALID( pxPeer ) )
    {
        return -1;
    }

    pthread_mutex_lock( &pxPeer->xStateMutex );

    if( pxPeer->xState != wsSTATE_OPEN )
    {
        lReturn = -1;
    }
    else
    {
        pxPeer->xState = wsSTATE_CLOSING;

        if( prvCreateThread( &pxPeer->xTimeoutThread, prvCloseTimeoutTask, pxPeer ) != 0 )
        {
            lReturn = -1;
        }
        else
        {
            pxPeer->xCloseThreadActive = true;
        }
    }

    pthread_mutex_unlock( &pxPeer->xStateMutex );
    return lReturn;
}
/*-----------------------------------------------------------*/

/*
 * prvSendFrame
 *
 * Encodes and sends one complete WebSocket frame. Builds the ( 2/4/10
 * byte ) header per RFC 6455 5.2 and ships it together with pcMessage via
 * prvSendAllIoV(), so no intermediate buffer holding header + payload is
 * ever allocated.
 */
static int prvSendFrame( WebSocketPeer_t * pxPeer, const char * pcMessage, uint64_t ullSize, int lOpcode )
{
    unsigned char pucHeader[ 10 ];
    uint8_t ucHeaderLen;
    struct iovec xIoVec[ 2 ];
    uint64_t ullLength;

    if( !wsCLIENT_IS_VALID( pxPeer ) )
    {
        return -1;
    }

    pucHeader[ 0 ] = ( wsFIN_BIT | lOpcode );
    ullLength = ullSize;

    if( ullLength <= 125U )
    {
        pucHeader[ 1 ] = ullLength & 0x7FU;
        ucHeaderLen = 2;
    }
    else if( ( ullLength >= 126U ) && ( ullLength <= 65535U ) )
    {
        pucHeader[ 1 ] = 126;
        pucHeader[ 2 ] = ( ullLength >> 8 ) & 255U;
        pucHeader[ 3 ] = ullLength & 255U;
        ucHeaderLen = 4;
    }
    else
    {
        pucHeader[ 1 ] = 127;
        pucHeader[ 2 ] = ( unsigned char ) ( ( ullLength >> 56 ) & 255U );
        pucHeader[ 3 ] = ( unsigned char ) ( ( ullLength >> 48 ) & 255U );
        pucHeader[ 4 ] = ( unsigned char ) ( ( ullLength >> 40 ) & 255U );
        pucHeader[ 5 ] = ( unsigned char ) ( ( ullLength >> 32 ) & 255U );
        pucHeader[ 6 ] = ( unsigned char ) ( ( ullLength >> 24 ) & 255U );
        pucHeader[ 7 ] = ( unsigned char ) ( ( ullLength >> 16 ) & 255U );
        pucHeader[ 8 ] = ( unsigned char ) ( ( ullLength >> 8 ) & 255U );
        pucHeader[ 9 ] = ( unsigned char ) ( ullLength & 255U );
        ucHeaderLen = 10;
    }

    if( ullLength > SIZE_MAX )
    {
        return -1;
    }

    xIoVec[ 0 ].iov_base = pucHeader;
    xIoVec[ 0 ].iov_len = ucHeaderLen;
    xIoVec[ 1 ].iov_base = ( void * ) pcMessage;
    xIoVec[ 1 ].iov_len = ( size_t ) ullLength;

    return ( int ) prvSendAllIoV( pxPeer, xIoVec, 2, MSG_NOSIGNAL );
}
/*-----------------------------------------------------------*/

int xWebSocketSendText( int xSocketFd, const char * pcMessage, uint64_t ullSize )
{
    return prvSendFrame( prvFindPeerById( xSocketFd ), pcMessage, ullSize, wsOPCODE_TEXT );
}
/*-----------------------------------------------------------*/

int xWebSocketSendBinary( int xSocketFd, const char * pcMessage, uint64_t ullSize )
{
    return prvSendFrame( prvFindPeerById( xSocketFd ), pcMessage, ullSize, wsOPCODE_BINARY );
}
/*-----------------------------------------------------------*/

int xWebSocketClosePeer( int xSocketFd )
{
    WebSocketPeer_t * pxPeer = prvFindPeerById( xSocketFd );
    unsigned char pucCloseCode[ 2 ];
    int lCode;

    if( !wsCLIENT_IS_VALID( pxPeer ) || ( pxPeer->xSocket == -1 ) )
    {
        return -1;
    }

    lCode = wsCLOSE_NORMAL;
    pucCloseCode[ 0 ] = ( lCode >> 8 );
    pucCloseCode[ 1 ] = ( lCode & 0xFF );

    if( prvSendFrame( pxPeer, ( const char * ) pucCloseCode, sizeof( pucCloseCode ), wsOPCODE_CLOSE ) < 0 )
    {
        shutdown( pxPeer->xSocket, SHUT_RDWR );
        return -1;
    }

    if( prvStartCloseTimeout( pxPeer ) < 0 )
    {
        shutdown( pxPeer->xSocket, SHUT_RDWR );
        return -1;
    }

    return 0;
}
/*-----------------------------------------------------------*/

const char * pcWebSocketGetPath( int xSocketFd )
{
    WebSocketPeer_t * pxPeer = prvFindPeerById( xSocketFd );

    if( !wsCLIENT_IS_VALID( pxPeer ) )
    {
        return NULL;
    }

    return pxPeer->pcPath;
}
/*-----------------------------------------------------------*/

/* prvIsControlFrame: non-zero for the three control opcodes
 * ( close/ping/pong ), which may not be fragmented. */
static inline int prvIsControlFrame( int lOpcode )
{
    return ( ( lOpcode == wsOPCODE_CLOSE ) || ( lOpcode == wsOPCODE_PING ) || ( lOpcode == wsOPCODE_PONG ) );
}
/*-----------------------------------------------------------*/

/*
 * prvDoHandshake
 *
 * Performs the HTTP -> WebSocket upgrade handshake for one peer. Reads
 * the request into pxFrame->pucFrameBuffer, runs the optional
 * pxOnAuthorise hook, then hands the buffer to xGetHandshakeResponse()
 * ( handshake.c ) to compute and send "101 Switching Protocols". On any
 * failure the connection is left for the caller to close; pxOnOpened is
 * never fired for a partial handshake.
 */
static int prvDoHandshake( WebSocketFrame_t * pxFrame )
{
    char * pcResponse;
    char * pcHeaderEnd;
    ssize_t xReceived;

    xReceived = wsRECV( pxFrame->pxPeer, pxFrame->pucFrameBuffer, sizeof( pxFrame->pucFrameBuffer ) - 1 );

    if( xReceived < 0 )
    {
        return -1;
    }

    /* NUL-terminate so the string scans below ( and the auth hook ) stay
     * in bounds even when this peer slot is being reused from a previous
     * connection. */
    pxFrame->pucFrameBuffer[ xReceived ] = '\0';

    pcHeaderEnd = strstr( ( const char * ) pxFrame->pucFrameBuffer, "\r\n\r\n" );

    if( pcHeaderEnd == NULL )
    {
        return -1;
    }

    pxFrame->xBytesRead = ( size_t ) xReceived;
    pxFrame->xCurrentPosition = ( size_t ) ( ( ptrdiff_t ) ( pcHeaderEnd - ( char * ) pxFrame->pucFrameBuffer ) ) + 4;
    prvSaveRequestPath( pxFrame->pxPeer, ( const char * ) pxFrame->pucFrameBuffer );

    /* Authorise the upgrade before we commit to it. Must run before
     * xGetHandshakeResponse(), which tokenises pucFrameBuffer in place. */
    if( ( pxFrame->pxPeer->pxServer->xEvents.pxOnAuthorise != NULL ) &&
        ( pxFrame->pxPeer->pxServer->xEvents.pxOnAuthorise(
              pxFrame->pxPeer->lClientId, ( const char * ) pxFrame->pucFrameBuffer ) != 0 ) )
    {
        static const char pcDeny[] =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Connection: close\r\n"
            "Content-Length: 0\r\n"
            "\r\n";

        wsSEND( pxFrame->pxPeer, pcDeny, sizeof( pcDeny ) - 1 );
        return -1;
    }

    if( xGetHandshakeResponse( ( char * ) pxFrame->pucFrameBuffer, &pcResponse ) < 0 )
    {
        return -1;
    }

    if( wsSEND( pxFrame->pxPeer, pcResponse, strlen( pcResponse ) ) < 0 )
    {
        free( pcResponse );
        return -1;
    }

    prvSetPeerState( pxFrame->pxPeer, wsSTATE_OPEN );

    if( pxFrame->pxPeer->pxServer->xEvents.pxOnOpened != NULL )
    {
        pxFrame->pxPeer->pxServer->xEvents.pxOnOpened( pxFrame->pxPeer->lClientId );
    }

    free( pcResponse );
    return 0;
}
/*-----------------------------------------------------------*/

/*
 * prvSendCloseFrame
 *
 * Sends a CLOSE frame, either echoing the peer's close code or a
 * caller-supplied override.
 *
 * @param pxFrame   Active frame context ( its pucControlMessage buffer is
 *                   reused ).
 * @param lCloseCode Pass -1 to echo/validate the peer's own close payload
 *                   ( pxFrame->pucControlMessage ), or an explicit
 *                   RFC 6455 code ( e.g. wsCLOSE_MESSAGE_TOO_BIG ) to send
 *                   instead.
 */
static int prvSendCloseFrame( WebSocketFrame_t * pxFrame, int lCloseCode )
{
    int lCode;

    if( lCloseCode != -1 )
    {
        lCode = lCloseCode;
    }
    else if( ( pxFrame->ullFrameSize == 0U ) || ( pxFrame->ullFrameSize > 2U ) )
    {
        if( prvSendFrame( pxFrame->pxPeer, ( const char * ) pxFrame->pucControlMessage, pxFrame->ullFrameSize,
                           wsOPCODE_CLOSE ) < 0 )
        {
            return -1;
        }

        return 0;
    }
    else
    {
        if( pxFrame->ullFrameSize == 1U )
        {
            lCode = pxFrame->pucControlMessage[ 0 ];
        }
        else
        {
            lCode = ( ( int ) pxFrame->pucControlMessage[ 0 ] ) << 8 | pxFrame->pucControlMessage[ 1 ];
        }

        if( ( ( lCode < 1000 ) || ( lCode > 1003 ) ) && ( ( lCode < 1007 ) || ( lCode > 1011 ) ) &&
            ( ( lCode < 3000 ) || ( lCode > 4999 ) ) )
        {
            lCode = wsCLOSE_PROTOCOL_ERROR;
        }
        else
        {
            /* The peer's own close code is already well-formed: echo the
             * two bytes back exactly as received. */
            if( prvSendFrame( pxFrame->pxPeer, ( const char * ) pxFrame->pucControlMessage, sizeof( char ) * 2,
                               wsOPCODE_CLOSE ) < 0 )
            {
                return -1;
            }

            return 0;
        }
    }

    pxFrame->pucControlMessage[ 0 ] = ( lCode >> 8 );
    pxFrame->pucControlMessage[ 1 ] = ( lCode & 0xFF );

    if( prvSendFrame( pxFrame->pxPeer, ( const char * ) pxFrame->pucControlMessage, sizeof( char ) * 2,
                       wsOPCODE_CLOSE ) < 0 )
    {
        return -1;
    }

    return 0;
}
/*-----------------------------------------------------------*/

/*
 * prvNextByte
 *
 * Returns the next byte off the wire, refilling pucFrameBuffer via
 * recv() when exhausted. Kept static inline since it is called once per
 * payload byte on the hottest path in the parser ( prvReadSingleFrame()'s
 * unmasking loop ).
 */
static inline int prvNextByte( WebSocketFrame_t * pxFrame )
{
    ssize_t xReceived;

    if( ( pxFrame->xCurrentPosition == 0U ) || ( pxFrame->xCurrentPosition == pxFrame->xBytesRead ) )
    {
        xReceived = wsRECV( pxFrame->pxPeer, pxFrame->pucFrameBuffer, sizeof( pxFrame->pucFrameBuffer ) );

        if( xReceived <= 0 )
        {
            pxFrame->xError = 1;
            return -1;
        }

        pxFrame->xBytesRead = ( size_t ) xReceived;
        pxFrame->xCurrentPosition = 0;
    }

    return pxFrame->pucFrameBuffer[ pxFrame->xCurrentPosition++ ];
}
/*-----------------------------------------------------------*/

/* prvCheckedAddU64: adds two uint64_t, reporting overflow instead of
 * wrapping silently. */
static bool prvCheckedAddU64( uint64_t ullA, uint64_t ullB, uint64_t * pullOut )
{
    if( ullA > ( UINT64_MAX - ullB ) )
    {
        return false;
    }

    *pullOut = ullA + ullB;
    return true;
}
/*-----------------------------------------------------------*/

/*
 * prvValidateUtf8Text
 *
 * Incrementally validates UTF-8 for a text message ( a no-op unless built
 * with -DVALIDATE_UTF8 ). On the final fragment ( ucIsFin ) the whole
 * message is checked at once and a protocol-error CLOSE is sent on
 * failure; on intermediate fragments the decoder state is carried forward
 * in pxState->ulUtf8State so previously-checked bytes are never
 * re-scanned.
 */
static int prvValidateUtf8Text( WebSocketFrame_t * pxFrame, WebSocketFrameState_t * pxState )
{
    #ifndef VALIDATE_UTF8
        ( void ) pxFrame;
        ( void ) pxState;
    #endif

    #ifdef VALIDATE_UTF8
        if( pxFrame->lFrameType != wsOPCODE_TEXT )
        {
            return 0;
        }

        if( pxState->ucIsFin )
        {
            if( ulUtf8IsValidLengthState(
                    pxState->pucMessageData + ( pxState->ullDataMessageIndex - pxState->ullFrameLength ),
                    pxState->ullFrameLength, pxState->ulUtf8State ) != utf8ACCEPT )
            {
                pxFrame->xError = 1;
                prvSendCloseFrame( pxFrame, wsCLOSE_INVALID_PAYLOAD );
            }

            return 0;
        }

        pxState->ulUtf8State =
            ulUtf8IsValidLengthState( pxState->pucMessageData + ( pxState->ullDataMessageIndex - pxState->ullFrameLength ),
                                       pxState->ullFrameLength, pxState->ulUtf8State );

        if( pxState->ulUtf8State == utf8REJECT )
        {
            pxFrame->xError = 1;
            prvSendCloseFrame( pxFrame, wsCLOSE_INVALID_PAYLOAD );
        }
    #endif

    return 0;
}
/*-----------------------------------------------------------*/

/* prvHandlePongFrame: records a PONG's echoed ping id as lLastPongId,
 * ignoring stale/future ids. */
static int prvHandlePongFrame( WebSocketFrame_t * pxFrame, WebSocketFrameState_t * pxState )
{
    pxState->ucIsFin = 0;

    if( pxState->ullFrameSize != sizeof( pxFrame->pxPeer->lLastPongId ) )
    {
        return 0;
    }

    pthread_mutex_lock( &pxFrame->pxPeer->xPingMutex );
    pxState->lPongId = ( pxState->pucControlMessage[ 3 ] << 0 ) |
                        ( pxState->pucControlMessage[ 2 ] << 8 ) |
                        ( pxState->pucControlMessage[ 1 ] << 16 ) |
                        ( pxState->pucControlMessage[ 0 ] << 24 );

    if( ( pxState->lPongId < 0 ) || ( pxState->lPongId > pxFrame->pxPeer->lCurrentPingId ) )
    {
        pthread_mutex_unlock( &pxFrame->pxPeer->xPingMutex );
        return 0;
    }

    pxFrame->pxPeer->lLastPongId = pxState->lPongId;
    pthread_mutex_unlock( &pxFrame->pxPeer->xPingMutex );

    return 0;
}
/*-----------------------------------------------------------*/

/* prvHandleCloseFrame: finalises a received CLOSE frame into pxFrame for
 * the caller to act on. */
static int prvHandleCloseFrame( WebSocketFrame_t * pxFrame, WebSocketFrameState_t * pxState )
{
    #ifdef VALIDATE_UTF8
        if( ( pxState->ullFrameSize > 2U ) &&
            !xUtf8IsValidLength( pxState->pucControlMessage + 2, pxState->ullFrameSize - 2 ) )
        {
            pxFrame->xError = 1;
            return -1;
        }
    #endif

    pxFrame->ullFrameSize = pxState->ullFrameSize;
    pxFrame->lFrameType = wsOPCODE_CLOSE;
    free( pxState->pucMessageData );
    return 0;
}
/*-----------------------------------------------------------*/

/*
 * prvReadSingleFrame
 *
 * Reads the length/mask/payload of a single wire frame into either
 * pxState->pucMessageData ( data frames ) or pxState->pucControlMessage
 * ( control frames ), unmasking each byte as it arrives. The payload
 * buffer is grown with realloc() exactly once per frame ( sized to fit
 * the new bytes plus, on the final fragment, the trailing NUL ) rather
 * than byte-by-byte, keeping reassembly of large/fragmented messages
 * allocation-cheap.
 */
static int prvReadSingleFrame( WebSocketFrame_t * pxFrame, WebSocketFrameState_t * pxState )
{
    uint64_t * pullTotalSize;
    uint64_t ullNextTotalSize = 0;
    uint64_t ullAllocSize = 0;
    unsigned char * pucGrown;
    unsigned char * pucMessage;
    uint64_t * pullMsgIndex;
    uint8_t * pucMasks;
    int lCurrentByte;
    uint64_t ullIndex;

    if( prvIsControlFrame( pxState->ucOpcode ) )
    {
        pullTotalSize = &pxState->ullFrameSize;
        pullMsgIndex = &pxState->ullControlMessageIndex;
        pucMasks = pxState->ucControlMasks;
        pucMessage = pxState->pucControlMessage;
    }
    else
    {
        pullTotalSize = &pxFrame->ullFrameSize;
        pullMsgIndex = &pxState->ullDataMessageIndex;
        pucMasks = pxState->ucDataMasks;
        pucMessage = pxState->pucMessageData;
    }

    if( pxState->ullFrameLength == 126U )
    {
        pxState->ullFrameLength = ( ( ( uint64_t ) prvNextByte( pxFrame ) ) << 8 ) | prvNextByte( pxFrame );
    }
    else if( pxState->ullFrameLength == 127U )
    {
        pxState->ullFrameLength =
            ( ( ( uint64_t ) prvNextByte( pxFrame ) ) << 56 ) |
            ( ( ( uint64_t ) prvNextByte( pxFrame ) ) << 48 ) |
            ( ( ( uint64_t ) prvNextByte( pxFrame ) ) << 40 ) |
            ( ( ( uint64_t ) prvNextByte( pxFrame ) ) << 32 ) |
            ( ( ( uint64_t ) prvNextByte( pxFrame ) ) << 24 ) |
            ( ( ( uint64_t ) prvNextByte( pxFrame ) ) << 16 ) |
            ( ( ( uint64_t ) prvNextByte( pxFrame ) ) << 8 ) |
            ( ( uint64_t ) prvNextByte( pxFrame ) );
    }
    else
    {
        /* 7-bit length already decoded by the caller; nothing to extend. */
    }

    if( !prvCheckedAddU64( *pullTotalSize, pxState->ullFrameLength, &ullNextTotalSize ) ||
        ( ullNextTotalSize > wsMAX_FRAME_LENGTH ) )
    {
        prvSendCloseFrame( pxFrame, wsCLOSE_MESSAGE_TOO_BIG );
        pxFrame->xError = 1;
        return -1;
    }

    *pullTotalSize = ullNextTotalSize;

    pucMasks[ 0 ] = prvNextByte( pxFrame );
    pucMasks[ 1 ] = prvNextByte( pxFrame );
    pucMasks[ 2 ] = prvNextByte( pxFrame );
    pucMasks[ 3 ] = prvNextByte( pxFrame );

    if( pxFrame->xError )
    {
        return -1;
    }

    if( pxState->ullFrameLength > 0U )
    {
        if( !prvIsControlFrame( pxState->ucOpcode ) )
        {
            if( !prvCheckedAddU64( *pullMsgIndex, pxState->ullFrameLength, &ullAllocSize ) ||
                !prvCheckedAddU64( ullAllocSize, pxState->ucIsFin, &ullAllocSize ) ||
                ( ullAllocSize > wsMAX_FRAME_LENGTH + 1U ) )
            {
                prvSendCloseFrame( pxFrame, wsCLOSE_MESSAGE_TOO_BIG );
                pxFrame->xError = 1;
                return -1;
            }

            pucGrown = realloc( pucMessage, ullAllocSize );

            if( pucGrown == NULL )
            {
                prvSendCloseFrame( pxFrame, wsCLOSE_MESSAGE_TOO_BIG );
                pxFrame->xError = 1;
                return -1;
            }

            pucMessage = pucGrown;
            pxState->pucMessageData = pucMessage;
        }

        for( ullIndex = 0; ullIndex < pxState->ullFrameLength; ullIndex++, ( *pullMsgIndex )++ )
        {
            lCurrentByte = prvNextByte( pxFrame );

            if( lCurrentByte == -1 )
            {
                return -1;
            }

            pucMessage[ *pullMsgIndex ] = lCurrentByte ^ pucMasks[ ullIndex % 4U ];
        }
    }

    if( pxState->ucIsFin && ( *pullTotalSize > 0U ) )
    {
        if( ( pxState->ullFrameLength == 0U ) && !prvIsControlFrame( pxState->ucOpcode ) )
        {
            pucGrown = realloc( pucMessage, *pullMsgIndex + 1U );

            if( pucGrown == NULL )
            {
                pxFrame->xError = 1;
                return -1;
            }

            pucMessage = pucGrown;
            pxState->pucMessageData = pucMessage;
        }

        pucMessage[ *pullMsgIndex ] = '\0';
    }

    return 0;
}
/*-----------------------------------------------------------*/

/*
 * prvGetNextCompleteFrame
 *
 * Reads and reassembles the next complete WebSocket message. Loops over
 * prvReadSingleFrame() across as many continuation frames as needed,
 * transparently answering PING with PONG and tracking PONG replies to our
 * own pings, until a FIN frame completes the message or a CLOSE frame
 * arrives. On success, pxFrame->pucMessage/ullFrameSize/lFrameType
 * describe the result; the caller owns and must free() pxFrame->pucMessage.
 */
static int prvGetNextCompleteFrame( WebSocketFrame_t * pxFrame )
{
    WebSocketFrameState_t xState = { 0 };

    xState.pucMessageData = NULL;
    xState.pucControlMessage = pxFrame->pucControlMessage;

    #ifdef VALIDATE_UTF8
        xState.ulUtf8State = utf8ACCEPT;
    #endif

    pxFrame->ullFrameSize = 0;
    pxFrame->lFrameType = -1;
    pxFrame->pucMessage = NULL;

    do
    {
        xState.lCurrentByte = prvNextByte( pxFrame );

        if( xState.lCurrentByte == -1 )
        {
            return -1;
        }

        xState.ucIsFin = ( xState.lCurrentByte & 0xFF ) >> wsFIN_SHIFT;
        xState.ucOpcode = ( xState.lCurrentByte & 0xF );

        if( xState.lCurrentByte & 0x70 )
        {
            pxFrame->xError = 1;
            break;
        }

        if( ( ( pxFrame->lFrameType == -1 ) && ( xState.ucOpcode == wsOPCODE_CONTINUATION ) ) ||
            ( ( pxFrame->lFrameType != -1 ) && !prvIsControlFrame( xState.ucOpcode ) &&
              ( xState.ucOpcode != wsOPCODE_CONTINUATION ) ) )
        {
            pxFrame->xError = 1;
            break;
        }

        if( ( xState.ucOpcode != wsOPCODE_TEXT ) &&
            ( xState.ucOpcode != wsOPCODE_BINARY ) &&
            ( xState.ucOpcode != wsOPCODE_CONTINUATION ) &&
            ( xState.ucOpcode != wsOPCODE_PING ) &&
            ( xState.ucOpcode != wsOPCODE_PONG ) &&
            ( xState.ucOpcode != wsOPCODE_CLOSE ) )
        {
            pxFrame->lFrameType = xState.ucOpcode;
            pxFrame->xError = 1;
            break;
        }

        if( ( prvGetPeerState( pxFrame->pxPeer ) == wsSTATE_CLOSING ) && ( xState.ucOpcode != wsOPCODE_CLOSE ) )
        {
            pxFrame->xError = 1;
            break;
        }

        if( ( xState.ucOpcode != wsOPCODE_CONTINUATION ) && !prvIsControlFrame( xState.ucOpcode ) )
        {
            pxFrame->lFrameType = xState.ucOpcode;
        }

        xState.ucMask = prvNextByte( pxFrame );
        xState.ullFrameLength = xState.ucMask & 0x7FU;
        xState.ullFrameSize = 0;
        xState.ullControlMessageIndex = 0;

        if( prvIsControlFrame( xState.ucOpcode ) && ( !xState.ucIsFin || ( xState.ullFrameLength > 125U ) ) )
        {
            pxFrame->xError = 1;
            break;
        }

        if( prvReadSingleFrame( pxFrame, &xState ) < 0 )
        {
            break;
        }

        switch( xState.ucOpcode )
        {
            case wsOPCODE_CONTINUATION:
            case wsOPCODE_TEXT:
                prvValidateUtf8Text( pxFrame, &xState );
                break;

            case wsOPCODE_PONG:
                prvHandlePongFrame( pxFrame, &xState );
                break;

            case wsOPCODE_PING:

                if( prvSendFrame( pxFrame->pxPeer, ( const char * ) pxFrame->pucControlMessage, xState.ullFrameSize,
                                   wsOPCODE_PONG ) < 0 )
                {
                    pxFrame->xError = 1;
                    goto DONE;
                }

                xState.ucIsFin = 0;
                break;

            case wsOPCODE_CLOSE:

                if( prvHandleCloseFrame( pxFrame, &xState ) < 0 )
                {
                    goto DONE;
                }

                return 0;

            default:
                break;
        }
    } while( !xState.ucIsFin && !pxFrame->xError );

DONE:

    if( pxFrame->xError )
    {
        free( xState.pucMessageData );
        pxFrame->pucMessage = NULL;
        return -1;
    }

    pxFrame->pucMessage = xState.pucMessageData;
    return 0;
}
/*-----------------------------------------------------------*/

/*
 * prvPeerTask
 *
 * Per-peer worker thread: runs the handshake, then the receive/dispatch
 * loop, until the connection closes.
 */
static void * prvPeerTask( void * pvArgs )
{
    WebSocketFrame_t xFrame = { 0 };
    WebSocketPeer_t * pxPeer = pvArgs;

    xFrame.pxPeer = pxPeer;

    if( prvDoHandshake( &xFrame ) < 0 )
    {
        goto FORCE_CLOSE;
    }

    while( prvGetNextCompleteFrame( &xFrame ) >= 0 )
    {
        if( ( ( xFrame.lFrameType == wsOPCODE_TEXT ) || ( xFrame.lFrameType == wsOPCODE_BINARY ) ) &&
            !xFrame.xError )
        {
            if( pxPeer->pxServer->xEvents.pxOnHandle != NULL )
            {
                pxPeer->pxServer->xEvents.pxOnHandle( pxPeer->lClientId, xFrame.pucMessage, xFrame.ullFrameSize,
                                                       xFrame.lFrameType );
            }
        }
        else if( ( xFrame.lFrameType == wsOPCODE_CLOSE ) && !xFrame.xError )
        {
            if( prvGetPeerState( pxPeer ) != wsSTATE_CLOSING )
            {
                prvSetPeerState( pxPeer, wsSTATE_CLOSING );
                prvSendCloseFrame( &xFrame, -1 );
            }

            free( xFrame.pucMessage );
            break;
        }
        else
        {
            /* Nothing to dispatch for this iteration ( a control frame
             * other than CLOSE was already handled inline ). */
        }

        free( xFrame.pucMessage );
        xFrame.pucMessage = NULL;
    }

    if( pxPeer->pxServer->xEvents.pxOnClosed != NULL )
    {
        pxPeer->pxServer->xEvents.pxOnClosed( pxPeer->lClientId );
    }

FORCE_CLOSE:

    if( pxPeer->xCloseThreadActive )
    {
        pthread_cond_signal( &pxPeer->xCloseCond );
        pthread_join( pxPeer->xTimeoutThread, NULL );
    }

    if( prvGetPeerState( pxPeer ) != wsSTATE_CLOSED )
    {
        prvClosePeer( pxPeer, 1 );
    }

    return NULL;
}
/*-----------------------------------------------------------*/

/*
 * prvAcceptTask
 *
 * Accept-loop thread: one per server, hands each new connection off to a
 * fresh prvPeerTask().
 */
static void * prvAcceptTask( void * pvArgs )
{
    struct sockaddr_storage xPeerAddr;
    socklen_t xPeerAddrLen = sizeof( xPeerAddr );
    WebSocketHandle_t pxServer = ( WebSocketHandle_t ) pvArgs;
    int lNewSocket;
    uint8_t ucSlot;
    WebSocketPeer_t * pxPeer;

    while( pxServer->xRunning )
    {
        lNewSocket = accept( pxServer->xSocket, ( struct sockaddr * ) &xPeerAddr, &xPeerAddrLen );

        if( lNewSocket < 0 )
        {
            if( errno == EINTR )
            {
                continue;
            }

            if( !pxServer->xRunning )
            {
                break;
            }

            struct timespec xRetryDelay = { 0, 10 * 1000 * 1000 };
            nanosleep( &xRetryDelay, NULL );
            continue;
        }

        if( pxServer->ulTimeoutMs )
        {
            struct timeval xSndTimeout = {
                .tv_sec = pxServer->ulTimeoutMs / 1000,
                .tv_usec = ( pxServer->ulTimeoutMs % 1000 ) * 1000
            };
            setsockopt( lNewSocket, SOL_SOCKET, SO_SNDTIMEO, ( const char * ) &xSndTimeout, sizeof( xSndTimeout ) );
        }

        #ifdef TCP_NODELAY
            {
                int lNoDelay = 1;
                setsockopt( lNewSocket, IPPROTO_TCP, TCP_NODELAY, ( const char * ) &lNoDelay, sizeof( lNoDelay ) );
            }
        #endif

        pthread_mutex_lock( &xGeneralMutex );
        pthread_mutex_lock( &pxServer->xClientsMutex );

        for( ucSlot = 0; ucSlot < pxServer->ucTotalPeers; ++ucSlot )
        {
            pxPeer = &pxServer->pxClients[ ucSlot ];

            if( pxPeer->xSocket == -1 )
            {
                pxPeer->pxServer = pxServer;
                pxPeer->xSocket = lNewSocket;
                pxPeer->xState = wsSTATE_CONNECTING;
                pxPeer->xCloseThreadActive = false;
                pxPeer->lLastPongId = -1;
                pxPeer->lCurrentPingId = -1;
                pxPeer->lClientId = ( int ) ulNextClientId++;
                pthread_mutex_init( &pxPeer->xStateMutex, NULL );
                pthread_mutex_init( &pxPeer->xSendMutex, NULL );
                pthread_mutex_init( &pxPeer->xPingMutex, NULL );
                pthread_cond_init( &pxPeer->xCloseCond, NULL );
                break;
            }
        }

        pthread_mutex_unlock( &pxServer->xClientsMutex );
        pthread_mutex_unlock( &xGeneralMutex );

        if( ucSlot != pxServer->ucTotalPeers )
        {
            pthread_t xThreadId = 0;
            prvCreateThread( &xThreadId, prvPeerTask, &pxServer->pxClients[ ucSlot ] );
            pthread_detach( xThreadId );
        }
        else
        {
            /* Pool exhausted: refuse the connection instead of blocking
             * accept(). */
            prvCloseSocket( lNewSocket );
        }
    }

    return NULL;
}
/*-----------------------------------------------------------*/

/* prvBindSocket: resolves host:port and returns a bound ( but not yet
 * listening ) socket, or a negative value on failure. */
static int prvBindSocket( WebSocketHandle_t pxServer )
{
    struct addrinfo xHints = {
        .ai_flags = AI_PASSIVE,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM
    };
    char pcPortString[ 8 ] = { 0 };
    struct addrinfo * pxResolved = NULL;
    struct addrinfo * pxCandidate;
    int lFd = -1;
    int lReuse = 1;

    snprintf( pcPortString, sizeof( pcPortString ), "%u", pxServer->usPort );

    if( getaddrinfo( pxServer->pcHost, pcPortString, &xHints, &pxResolved ) != 0 )
    {
        return -1;
    }

    for( pxCandidate = pxResolved; pxCandidate != NULL; pxCandidate = pxCandidate->ai_next )
    {
        lFd = socket( pxCandidate->ai_family, pxCandidate->ai_socktype, pxCandidate->ai_protocol );

        if( lFd < 0 )
        {
            continue;
        }

        setsockopt( lFd, SOL_SOCKET, SO_REUSEADDR, &lReuse, sizeof( lReuse ) );

        if( bind( lFd, pxCandidate->ai_addr, pxCandidate->ai_addrlen ) == 0 )
        {
            break;
        }

        close( lFd );
        lFd = -1;
    }

    freeaddrinfo( pxResolved );

    if( lFd < 0 )
    {
        return -2;
    }

    return lFd;
}
/*-----------------------------------------------------------*/

/* prvCreateServer: allocates a server + its peer pool, binds/listens, and
 * starts the accept thread. */
static WebSocketHandle_t prvCreateServer( const char * pcHost, uint16_t usPort, uint32_t ulTimeoutMs,
                                           uint8_t ucTotalSlots, const WebSocketEvents_t * pxEvents )
{
    WebSocketHandle_t pxServer;
    uint8_t ucSlot;
    int lFd;

    if( ucTotalSlots == 0U )
    {
        return NULL;
    }

    pxServer = ( WebSocketHandle_t ) calloc( 1, sizeof( *pxServer ) );

    if( pxServer == NULL )
    {
        return NULL;
    }

    pxServer->pcHost = pcHost;
    pxServer->usPort = usPort;
    pxServer->ulTimeoutMs = ulTimeoutMs;
    pxServer->ucTotalPeers = ucTotalSlots;
    pxServer->xSocket = -1;

    if( pxEvents != NULL )
    {
        pxServer->xEvents = *pxEvents;
    }

    pxServer->pxClients = ( WebSocketPeer_t * ) calloc( ucTotalSlots, sizeof( *pxServer->pxClients ) );

    if( pxServer->pxClients == NULL )
    {
        free( pxServer );
        return NULL;
    }

    for( ucSlot = 0; ucSlot < ucTotalSlots; ++ucSlot )
    {
        pxServer->pxClients[ ucSlot ].xSocket = -1;
        pxServer->pxClients[ ucSlot ].pxServer = pxServer;
    }

    pthread_mutex_init( &pxServer->xClientsMutex, NULL );

    lFd = prvBindSocket( pxServer );

    if( lFd < 0 )
    {
        pthread_mutex_destroy( &pxServer->xClientsMutex );
        free( pxServer->pxClients );
        free( pxServer );
        return NULL;
    }

    if( listen( lFd, ucTotalSlots ) < 0 )
    {
        close( lFd );
        pthread_mutex_destroy( &pxServer->xClientsMutex );
        free( pxServer->pxClients );
        free( pxServer );
        return NULL;
    }

    pxServer->xSocket = lFd;
    pxServer->xRunning = true;

    pthread_mutex_lock( &xGeneralMutex );
    pxServer->pxNext = pxServerList;
    pxServerList = pxServer;
    pthread_mutex_unlock( &xGeneralMutex );

    if( prvCreateThread( &pxServer->xThread, prvAcceptTask, pxServer ) != 0 )
    {
        WebSocketHandle_t pxIter;

        pthread_mutex_lock( &xGeneralMutex );

        if( pxServerList == pxServer )
        {
            pxServerList = pxServer->pxNext;
        }
        else
        {
            for( pxIter = pxServerList; pxIter != NULL; pxIter = pxIter->pxNext )
            {
                if( pxIter->pxNext == pxServer )
                {
                    pxIter->pxNext = pxServer->pxNext;
                    break;
                }
            }
        }

        pthread_mutex_unlock( &xGeneralMutex );

        pxServer->xRunning = false;
        prvCloseSocket( pxServer->xSocket );
        pthread_mutex_destroy( &pxServer->xClientsMutex );
        free( pxServer->pxClients );
        free( pxServer );
        return NULL;
    }

    return pxServer;
}
/*-----------------------------------------------------------*/

WebSocketHandle_t xWebSocketCreate( const char * pcHost, uint16_t usPort, uint32_t ulTimeoutMs, uint8_t ucTotalSlots )
{
    return prvCreateServer( pcHost, usPort, ulTimeoutMs, ucTotalSlots, NULL );
}
/*-----------------------------------------------------------*/

void vWebSocketSetEvents( WebSocketHandle_t xServer, const WebSocketEvents_t * pxEvents )
{
    if( ( xServer != NULL ) && ( pxEvents != NULL ) )
    {
        xServer->xEvents = *pxEvents;
    }
}
/*-----------------------------------------------------------*/

void vWebSocketDelete( WebSocketHandle_t xServer )
{
    WebSocketHandle_t pxIter;

    if( xServer == NULL )
    {
        return;
    }

    xServer->xRunning = false;
    prvCloseSocket( xServer->xSocket );
    xServer->xSocket = -1;
    pthread_join( xServer->xThread, NULL );

    pthread_mutex_lock( &xGeneralMutex );

    if( pxServerList == xServer )
    {
        pxServerList = xServer->pxNext;
    }
    else
    {
        for( pxIter = pxServerList; pxIter != NULL; pxIter = pxIter->pxNext )
        {
            if( pxIter->pxNext == xServer )
            {
                pxIter->pxNext = xServer->pxNext;
                break;
            }
        }
    }

    pthread_mutex_unlock( &xGeneralMutex );

    pthread_mutex_destroy( &xServer->xClientsMutex );
    free( xServer->pxClients );
    free( xServer );
}
/*-----------------------------------------------------------*/
