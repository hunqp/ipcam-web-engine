/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "base64.h"

/* The 64-character Base64 alphabet ( RFC 1341 5.2 ), plus the trailing
 * NUL that sizeof() picks up and every scan below stops at. */
static const unsigned char ucBase64Table[ 65 ] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*-----------------------------------------------------------*/

unsigned char * pucBase64Encode( const unsigned char * pucSource, size_t xSourceLen, size_t * pxDestLen )
{
    unsigned char * pucOut;
    unsigned char * pucPos;
    const unsigned char * pucEnd;
    const unsigned char * pucIn;
    size_t xOutLen;
    int lLineLen;

    xOutLen = xSourceLen * 4 / 3 + 4; /* 3-byte blocks to 4-byte. */
    xOutLen += xOutLen / 72;          /* Line feeds.              */
    xOutLen++;                        /* NUL termination.         */

    if( xOutLen < xSourceLen )
    {
        return NULL; /* Integer overflow. */
    }

    pucOut = malloc( xOutLen );

    if( pucOut == NULL )
    {
        return NULL;
    }

    pucEnd = pucSource + xSourceLen;
    pucIn = pucSource;
    pucPos = pucOut;
    lLineLen = 0;

    while( ( pucEnd - pucIn ) >= 3 )
    {
        *pucPos++ = ucBase64Table[ pucIn[ 0 ] >> 2 ];
        *pucPos++ = ucBase64Table[ ( ( pucIn[ 0 ] & 0x03 ) << 4 ) | ( pucIn[ 1 ] >> 4 ) ];
        *pucPos++ = ucBase64Table[ ( ( pucIn[ 1 ] & 0x0f ) << 2 ) | ( pucIn[ 2 ] >> 6 ) ];
        *pucPos++ = ucBase64Table[ pucIn[ 2 ] & 0x3f ];
        pucIn += 3;
        lLineLen += 4;

        if( lLineLen >= 72 )
        {
            *pucPos++ = '\n';
            lLineLen = 0;
        }
    }

    if( pucEnd - pucIn )
    {
        *pucPos++ = ucBase64Table[ pucIn[ 0 ] >> 2 ];

        if( ( pucEnd - pucIn ) == 1 )
        {
            *pucPos++ = ucBase64Table[ ( pucIn[ 0 ] & 0x03 ) << 4 ];
            *pucPos++ = '=';
        }
        else
        {
            *pucPos++ = ucBase64Table[ ( ( pucIn[ 0 ] & 0x03 ) << 4 ) | ( pucIn[ 1 ] >> 4 ) ];
            *pucPos++ = ucBase64Table[ ( pucIn[ 1 ] & 0x0f ) << 2 ];
        }

        *pucPos++ = '=';
        lLineLen += 4;
    }

    if( lLineLen )
    {
        *pucPos++ = '\n';
    }

    *pucPos = '\0';

    if( pxDestLen != NULL )
    {
        *pxDestLen = pucPos - pucOut;
    }

    return pucOut;
}
/*-----------------------------------------------------------*/

unsigned char * pucBase64Decode( const unsigned char * pucSource, size_t xSourceLen, size_t * pxDestLen )
{
    unsigned char ucDecodeTable[ 256 ];
    unsigned char * pucOut;
    unsigned char * pucPos;
    unsigned char ucBlock[ 4 ];
    unsigned char ucTemp;
    size_t xIndex;
    size_t xCount;
    size_t xOutLen;
    int lPadCount = 0;

    memset( ucDecodeTable, 0x80, 256 );

    for( xIndex = 0; xIndex < sizeof( ucBase64Table ) - 1; xIndex++ )
    {
        ucDecodeTable[ ucBase64Table[ xIndex ] ] = ( unsigned char ) xIndex;
    }

    ucDecodeTable[ ( unsigned char ) '=' ] = 0;

    xCount = 0;

    for( xIndex = 0; xIndex < xSourceLen; xIndex++ )
    {
        if( ucDecodeTable[ pucSource[ xIndex ] ] != 0x80 )
        {
            xCount++;
        }
    }

    if( ( xCount == 0U ) || ( xCount % 4U ) )
    {
        return NULL;
    }

    xOutLen = xCount / 4 * 3;
    pucPos = pucOut = malloc( xOutLen );

    if( pucOut == NULL )
    {
        return NULL;
    }

    xCount = 0;

    for( xIndex = 0; xIndex < xSourceLen; xIndex++ )
    {
        ucTemp = ucDecodeTable[ pucSource[ xIndex ] ];

        if( ucTemp == 0x80 )
        {
            continue;
        }

        if( pucSource[ xIndex ] == '=' )
        {
            lPadCount++;
        }

        ucBlock[ xCount ] = ucTemp;
        xCount++;

        if( xCount == 4U )
        {
            *pucPos++ = ( ucBlock[ 0 ] << 2 ) | ( ucBlock[ 1 ] >> 4 );
            *pucPos++ = ( ucBlock[ 1 ] << 4 ) | ( ucBlock[ 2 ] >> 2 );
            *pucPos++ = ( ucBlock[ 2 ] << 6 ) | ucBlock[ 3 ];
            xCount = 0;

            if( lPadCount )
            {
                if( lPadCount == 1 )
                {
                    pucPos--;
                }
                else if( lPadCount == 2 )
                {
                    pucPos -= 2;
                }
                else
                {
                    /* Invalid padding. */
                    free( pucOut );
                    return NULL;
                }

                break;
            }
        }
    }

    *pxDestLen = pucPos - pucOut;
    return pucOut;
}
/*-----------------------------------------------------------*/
