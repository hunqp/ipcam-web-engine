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
#ifndef UTF8_DECODE_H
#define UTF8_DECODE_H

#include <inttypes.h>
#include <stddef.h>

/* UTF-8 decoder states. */
#define utf8ACCEPT    ( 0 )
#define utf8REJECT    ( 1 )

/**
 * xUtf8IsValid
 *
 * Checks whether a NUL-terminated UTF-8 string is valid.
 *
 * @param pucString Pointer to the NUL-terminated UTF-8 string.
 * @return Non-zero if valid, 0 if invalid.
 */
extern int xUtf8IsValid( uint8_t * pucString );

/**
 * xUtf8IsValidLength
 *
 * Checks whether a UTF-8 buffer of known length is valid.
 *
 * @param pucString Pointer to the UTF-8 buffer.
 * @param xLen      Length of the buffer, in bytes.
 * @return Non-zero if valid, 0 if invalid.
 */
extern int xUtf8IsValidLength( uint8_t * pucString, size_t xLen );

/**
 * ulUtf8IsValidLengthState
 *
 * Validates a UTF-8 buffer while carrying the decoder state across calls,
 * so a message split over multiple fragments can be validated
 * incrementally without re-scanning bytes already checked.
 *
 * @param pucString Pointer to the UTF-8 buffer.
 * @param xLen      Length of the buffer, in bytes.
 * @param ulState   Decoder state returned by a previous call ( utf8ACCEPT
 *                   for a fresh message ).
 * @return The updated decoder state; compare against utf8ACCEPT /
 *         utf8REJECT.
 */
extern uint32_t ulUtf8IsValidLengthState( uint8_t * pucString, size_t xLen, uint32_t ulState );

#endif
