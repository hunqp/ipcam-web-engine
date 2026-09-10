#include <stddef.h>
#include "sha1.h"

/* Rotates a 32-bit word left by lBits ( SHA-1 FIPS 180-1 5.a ). */
#define sha1CIRCULAR_SHIFT( lBits, ulWord )    ( ( ( ulWord ) << ( lBits ) ) | ( ( ulWord ) >> ( 32 - ( lBits ) ) ) )

/*-----------------------------------------------------------*/

/*
 * prvProcessMessageBlock
 *
 * Runs the SHA-1 compression function over the 64-byte block currently
 * held in pxContext->ucMessageBlock and folds the result into
 * pxContext->ulIntermediateHash. Resets the block write cursor to 0 so
 * the caller can start filling the next block.
 */
static void prvProcessMessageBlock( Sha1Context_t * pxContext )
{
    /* Constants defined in SHA-1 ( FIPS 180-1 7 ). */
    const uint32_t ulK[] = {
        0x5A827999,
        0x6ED9EBA1,
        0x8F1BBCDC,
        0xCA62C1D6
    };
    int lIndex;
    uint32_t ulTemp;
    uint32_t ulW[ 80 ] = { 0 };
    uint32_t ulA, ulB, ulC, ulD, ulE;

    for( lIndex = 0; lIndex < 16; lIndex++ )
    {
        ulW[ lIndex ] = ( uint32_t ) pxContext->ucMessageBlock[ lIndex * 4 ] << 24;
        ulW[ lIndex ] |= ( uint32_t ) pxContext->ucMessageBlock[ lIndex * 4 + 1 ] << 16;
        ulW[ lIndex ] |= ( uint32_t ) pxContext->ucMessageBlock[ lIndex * 4 + 2 ] << 8;
        ulW[ lIndex ] |= ( uint32_t ) pxContext->ucMessageBlock[ lIndex * 4 + 3 ];
    }

    for( lIndex = 16; lIndex < 80; lIndex++ )
    {
        ulW[ lIndex ] = sha1CIRCULAR_SHIFT( 1, ulW[ lIndex - 3 ] ^ ulW[ lIndex - 8 ] ^ ulW[ lIndex - 14 ] ^ ulW[ lIndex - 16 ] );
    }

    ulA = pxContext->ulIntermediateHash[ 0 ];
    ulB = pxContext->ulIntermediateHash[ 1 ];
    ulC = pxContext->ulIntermediateHash[ 2 ];
    ulD = pxContext->ulIntermediateHash[ 3 ];
    ulE = pxContext->ulIntermediateHash[ 4 ];

    for( lIndex = 0; lIndex < 20; lIndex++ )
    {
        ulTemp = sha1CIRCULAR_SHIFT( 5, ulA ) + ( ( ulB & ulC ) | ( ( ~ulB ) & ulD ) ) + ulE + ulW[ lIndex ] + ulK[ 0 ];
        ulE = ulD;
        ulD = ulC;
        ulC = sha1CIRCULAR_SHIFT( 30, ulB );
        ulB = ulA;
        ulA = ulTemp;
    }

    for( lIndex = 20; lIndex < 40; lIndex++ )
    {
        ulTemp = sha1CIRCULAR_SHIFT( 5, ulA ) + ( ulB ^ ulC ^ ulD ) + ulE + ulW[ lIndex ] + ulK[ 1 ];
        ulE = ulD;
        ulD = ulC;
        ulC = sha1CIRCULAR_SHIFT( 30, ulB );
        ulB = ulA;
        ulA = ulTemp;
    }

    for( lIndex = 40; lIndex < 60; lIndex++ )
    {
        ulTemp = sha1CIRCULAR_SHIFT( 5, ulA ) +
                 ( ( ulB & ulC ) | ( ulB & ulD ) | ( ulC & ulD ) ) + ulE + ulW[ lIndex ] + ulK[ 2 ];
        ulE = ulD;
        ulD = ulC;
        ulC = sha1CIRCULAR_SHIFT( 30, ulB );
        ulB = ulA;
        ulA = ulTemp;
    }

    for( lIndex = 60; lIndex < 80; lIndex++ )
    {
        ulTemp = sha1CIRCULAR_SHIFT( 5, ulA ) + ( ulB ^ ulC ^ ulD ) + ulE + ulW[ lIndex ] + ulK[ 3 ];
        ulE = ulD;
        ulD = ulC;
        ulC = sha1CIRCULAR_SHIFT( 30, ulB );
        ulB = ulA;
        ulA = ulTemp;
    }

    pxContext->ulIntermediateHash[ 0 ] += ulA;
    pxContext->ulIntermediateHash[ 1 ] += ulB;
    pxContext->ulIntermediateHash[ 2 ] += ulC;
    pxContext->ulIntermediateHash[ 3 ] += ulD;
    pxContext->ulIntermediateHash[ 4 ] += ulE;
    pxContext->sMessageBlockIndex = 0;
}
/*-----------------------------------------------------------*/

/*
 * prvPadMessage
 *
 * Appends the SHA-1 end-of-message padding ( FIPS 180-1 5 ) - a single
 * 0x80 byte, zero bytes, and the 64-bit bit-length - compressing an extra
 * block first if there is not enough room left in the current one.
 */
static void prvPadMessage( Sha1Context_t * pxContext )
{
    if( pxContext->sMessageBlockIndex > 55 )
    {
        pxContext->ucMessageBlock[ pxContext->sMessageBlockIndex++ ] = 0x80;

        while( pxContext->sMessageBlockIndex < 64 )
        {
            pxContext->ucMessageBlock[ pxContext->sMessageBlockIndex++ ] = 0;
        }

        prvProcessMessageBlock( pxContext );

        while( pxContext->sMessageBlockIndex < 56 )
        {
            pxContext->ucMessageBlock[ pxContext->sMessageBlockIndex++ ] = 0;
        }
    }
    else
    {
        pxContext->ucMessageBlock[ pxContext->sMessageBlockIndex++ ] = 0x80;

        while( pxContext->sMessageBlockIndex < 56 )
        {
            pxContext->ucMessageBlock[ pxContext->sMessageBlockIndex++ ] = 0;
        }
    }

    pxContext->ucMessageBlock[ 56 ] = pxContext->ulLengthLowBits >> 24;
    pxContext->ucMessageBlock[ 57 ] = pxContext->ulLengthLowBits >> 16;
    pxContext->ucMessageBlock[ 58 ] = pxContext->ulLengthLowBits >> 8;
    pxContext->ucMessageBlock[ 59 ] = pxContext->ulLengthLowBits;
    pxContext->ucMessageBlock[ 60 ] = pxContext->ulLengthHighBits >> 24;
    pxContext->ucMessageBlock[ 61 ] = pxContext->ulLengthHighBits >> 16;
    pxContext->ucMessageBlock[ 62 ] = pxContext->ulLengthHighBits >> 8;
    pxContext->ucMessageBlock[ 63 ] = pxContext->ulLengthHighBits;

    prvProcessMessageBlock( pxContext );
}
/*-----------------------------------------------------------*/

int xSha1Reset( Sha1Context_t * pxContext )
{
    if( pxContext == NULL )
    {
        return sha1NULL;
    }

    pxContext->ulLengthHighBits = 0;
    pxContext->ulLengthLowBits = 0;
    pxContext->sMessageBlockIndex = 0;

    pxContext->ulIntermediateHash[ 0 ] = 0x67452301;
    pxContext->ulIntermediateHash[ 1 ] = 0xEFCDAB89;
    pxContext->ulIntermediateHash[ 2 ] = 0x98BADCFE;
    pxContext->ulIntermediateHash[ 3 ] = 0x10325476;
    pxContext->ulIntermediateHash[ 4 ] = 0xC3D2E1F0;

    pxContext->xIsDigestComputed = 0;
    pxContext->xIsDigestCorrupted = 0;

    return sha1SUCCESS;
}
/*-----------------------------------------------------------*/

int xSha1Result( Sha1Context_t * pxContext, uint8_t pucMessageDigest[ sha1HASH_SIZE ] )
{
    int lIndex;

    if( ( pxContext == NULL ) || ( pucMessageDigest == NULL ) )
    {
        return sha1NULL;
    }

    if( pxContext->xIsDigestCorrupted )
    {
        return pxContext->xIsDigestCorrupted;
    }

    if( !pxContext->xIsDigestComputed )
    {
        prvPadMessage( pxContext );

        for( lIndex = 0; lIndex < 64; ++lIndex )
        {
            pxContext->ucMessageBlock[ lIndex ] = 0;
        }

        pxContext->ulLengthHighBits = 0;
        pxContext->ulLengthLowBits = 0;
        pxContext->xIsDigestComputed = 1;
    }

    for( lIndex = 0; lIndex < sha1HASH_SIZE; ++lIndex )
    {
        pucMessageDigest[ lIndex ] = pxContext->ulIntermediateHash[ lIndex >> 2 ] >> 8 * ( 3 - ( lIndex & 0x03 ) );
    }

    return sha1SUCCESS;
}
/*-----------------------------------------------------------*/

int xSha1Input( Sha1Context_t * pxContext, const uint8_t * pucMessage, unsigned ulSize )
{
    if( !ulSize )
    {
        return sha1SUCCESS;
    }

    if( ( pxContext == NULL ) || ( pucMessage == NULL ) )
    {
        return sha1NULL;
    }

    if( pxContext->xIsDigestComputed )
    {
        pxContext->xIsDigestCorrupted = sha1STATE_ERROR;
        return sha1STATE_ERROR;
    }

    if( pxContext->xIsDigestCorrupted )
    {
        return pxContext->xIsDigestCorrupted;
    }

    while( ulSize-- && !pxContext->xIsDigestCorrupted )
    {
        pxContext->ucMessageBlock[ pxContext->sMessageBlockIndex++ ] = ( *pucMessage & 0xFF );

        pxContext->ulLengthHighBits += 8;

        if( pxContext->ulLengthHighBits == 0 )
        {
            pxContext->ulLengthLowBits++;

            if( pxContext->ulLengthLowBits == 0 )
            {
                pxContext->xIsDigestCorrupted = 1;
            }
        }

        if( pxContext->sMessageBlockIndex == 64 )
        {
            prvProcessMessageBlock( pxContext );
        }

        pucMessage++;
    }

    return sha1SUCCESS;
}
/*-----------------------------------------------------------*/
