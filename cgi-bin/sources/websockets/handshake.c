/*
 * Copyright (C) 2016-2024  Davidson Francis <davidsondfgl@gmail.com>
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
#include "sha1.h"
#include "base64.h"
#include "websockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*-----------------------------------------------------------*/

/*
 * xGetHandshakeAccept
 *
 * Computes the "Sec-WebSocket-Accept" value for a previously received
 * "Sec-WebSocket-Key": SHA-1( key + RFC 6455 GUID ), base64-encoded.
 *
 * @param pcKey      The client's "Sec-WebSocket-Key" value.
 * @param ppucDest   Receives an allocated buffer holding the base64
 *                    accept string; the caller must free() it.
 *
 * @return 0 on success, a negative number otherwise.
 *
 * @attention This is part of the internal API and is documented just
 * for completeness.
 */
int xGetHandshakeAccept( char * pcKey, unsigned char ** ppucDest )
{
    unsigned char pucHash[ sha1HASH_SIZE ]; /* SHA-1 hash.                 */
    Sha1Context_t xShaContext;              /* SHA-1 context.              */
    char * pcKeyAndMagic;                   /* WebSocket key + magic string. */

    /* Invalid key. */
    if( pcKey == NULL )
    {
        return -1;
    }

    pcKeyAndMagic = calloc( 1, sizeof( char ) * ( wsKEY_LEN + wsMAGIC_STRING_LEN + 1 ) );

    if( pcKeyAndMagic == NULL )
    {
        return -1;
    }

    strncpy( pcKeyAndMagic, pcKey, wsKEY_LEN );
    strcat( pcKeyAndMagic, wsMAGIC_STRING );

    xSha1Reset( &xShaContext );
    xSha1Input( &xShaContext, ( const uint8_t * ) pcKeyAndMagic, wsKEY_MAGIC_LEN );
    xSha1Result( &xShaContext, pucHash );

    *ppucDest = pucBase64Encode( pucHash, sha1HASH_SIZE, NULL );
    *( *ppucDest + strlen( ( const char * ) *ppucDest ) - 1 ) = '\0';
    free( pcKeyAndMagic );
    return 0;
}
/*-----------------------------------------------------------*/

/*
 * prvStrStrICase
 *
 * Finds the first occurrence of pcNeedle in pcHaystack, case-insensitive.
 *
 * @param pcHaystack Target string to be searched.
 * @param pcNeedle   Substring to search for.
 *
 * @return A pointer to the start of the found substring, or NULL if not
 *         found.
 */
static const char * prvStrStrICase( const char * pcHaystack, const char * pcNeedle )
{
    size_t xLength;

    for( xLength = strlen( pcNeedle ); *pcHaystack; pcHaystack++ )
    {
        if( !strncasecmp( pcHaystack, pcNeedle, xLength ) )
        {
            return pcHaystack;
        }
    }

    return NULL;
}
/*-----------------------------------------------------------*/

/*
 * xGetHandshakeResponse
 *
 * Builds the complete "101 Switching Protocols" response for a client
 * handshake request. Tokenises pcRequest in place while scanning for the
 * "Sec-WebSocket-Key" header.
 *
 * @param pcRequest   Client request ( modified in place by strtok_r() ).
 * @param ppcResponse Receives an allocated, ready-to-send response
 *                     string; the caller must free() it.
 *
 * @return 0 on success, a negative number otherwise.
 *
 * @attention This is part of the internal API and is documented just
 * for completeness.
 */
int xGetHandshakeResponse( char * pcRequest, char ** ppcResponse )
{
    unsigned char * pucAccept; /* Accept message.     */
    char * pcSavePtr;          /* strtok_r() cursor.  */
    char * pcToken;            /* Current token.      */
    int lReturn;                /* Return value.       */

    pcSavePtr = NULL;

    for( pcToken = strtok_r( pcRequest, "\r\n", &pcSavePtr ); pcToken != NULL;
         pcToken = strtok_r( NULL, "\r\n", &pcSavePtr ) )
    {
        if( prvStrStrICase( pcToken, wsHANDSHAKE_KEY_HEADER ) != NULL )
        {
            break;
        }
    }

    /* Ensure that we have a valid pointer. */
    if( pcToken == NULL )
    {
        return -1;
    }

    pcSavePtr = NULL;
    pcToken = strtok_r( pcToken, " ", &pcSavePtr );
    pcToken = strtok_r( NULL, " ", &pcSavePtr );

    lReturn = xGetHandshakeAccept( pcToken, &pucAccept );

    if( lReturn < 0 )
    {
        return lReturn;
    }

    *ppcResponse = malloc( sizeof( char ) * wsHANDSHAKE_ACCEPT_LEN );

    if( *ppcResponse == NULL )
    {
        return -1;
    }

    strcpy( *ppcResponse, wsHANDSHAKE_ACCEPT_HEADER );
    strcat( *ppcResponse, ( const char * ) pucAccept );
    strcat( *ppcResponse, "\r\n\r\n" );

    free( pucAccept );
    return 0;
}
/*-----------------------------------------------------------*/
