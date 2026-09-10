/**
 * Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Amazing utf8 decoder & validator grabbed from:
 *   http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
 *
 * All rights goes to the original author.
 */

#include "utf8.h"

/* DFA transition table: bytes 0x00-0xFF map to a character class in the
 * first 256 entries, and ( state * 16 + class ) maps to the next decoder
 * state in the remainder. See the reference link above for the derivation. */
static const uint8_t ucUtf8Table[] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 00..1f */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 20..3f */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 40..5f */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 60..7f */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, /* 80..9f */
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, /* a0..bf */
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, /* c0..df */
    0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, /* e0..ef */
    0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, /* f0..ff */
    0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, /* s0..s0 */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, /* s1..s2 */
    1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, /* s3..s4 */
    1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, /* s5..s6 */
    1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* s7..s8 */
};

/*-----------------------------------------------------------*/

/* prvUtf8Decode: feeds one byte through the DFA, updating *pulState and
 * *pulCodepoint in place, and returns the new state ( compare against
 * utf8ACCEPT / utf8REJECT ). */
static uint32_t prvUtf8Decode( uint32_t * pulState, uint32_t * pulCodepoint, uint32_t ulByte )
{
    uint32_t ulType = ucUtf8Table[ ulByte ];

    *pulCodepoint = ( *pulState != utf8ACCEPT ) ?
                    ( ulByte & 0x3fU ) | ( *pulCodepoint << 6 ) :
                    ( 0xff >> ulType ) & ulByte;
    *pulState = ucUtf8Table[ 256 + ( *pulState ) * 16 + ulType ];

    return *pulState;
}
/*-----------------------------------------------------------*/

int xUtf8IsValid( uint8_t * pucString )
{
    uint32_t ulCodepoint;
    uint32_t ulState = 0;

    while( *pucString )
    {
        prvUtf8Decode( &ulState, &ulCodepoint, *pucString++ );
    }

    return ulState == utf8ACCEPT;
}
/*-----------------------------------------------------------*/

int xUtf8IsValidLength( uint8_t * pucString, size_t xLen )
{
    size_t xIndex;
    uint32_t ulCodepoint;
    uint32_t ulState = 0;

    for( xIndex = 0; xIndex < xLen; xIndex++ )
    {
        prvUtf8Decode( &ulState, &ulCodepoint, *pucString++ );
    }

    return ulState == utf8ACCEPT;
}
/*-----------------------------------------------------------*/

uint32_t ulUtf8IsValidLengthState( uint8_t * pucString, size_t xLen, uint32_t ulState )
{
    size_t xIndex;
    uint32_t ulCodepoint = 0;

    for( xIndex = 0; xIndex < xLen; xIndex++ )
    {
        prvUtf8Decode( &ulState, &ulCodepoint, *pucString++ );
    }

    return ulState;
}
/*-----------------------------------------------------------*/
