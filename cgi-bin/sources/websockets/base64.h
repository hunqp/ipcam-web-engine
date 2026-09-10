/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
#ifndef BASE64_H
#define BASE64_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * pucBase64Encode
 *
 * Encodes binary data into a newly allocated Base64 string.
 *
 * @param pucSource    Pointer to the source binary buffer.
 * @param xSourceLen   Length of the source buffer, in bytes.
 * @param pxDestLen    Optional; receives the length of the encoded output.
 *
 * @return Pointer to an allocated, NUL-terminated Base64 buffer, or NULL
 *         on failure. The caller owns the returned buffer and must
 *         free() it.
 */
unsigned char * pucBase64Encode( const unsigned char * pucSource, size_t xSourceLen, size_t * pxDestLen );

/**
 * pucBase64Decode
 *
 * Decodes a Base64 string into newly allocated binary data.
 *
 * @param pucSource    Pointer to the Base64-encoded buffer.
 * @param xSourceLen   Length of the Base64 buffer, in bytes.
 * @param pxDestLen    Receives the length of the decoded output.
 *
 * @return Pointer to an allocated decoded buffer, or NULL on failure.
 *         The caller owns the returned buffer and must free() it.
 */
unsigned char * pucBase64Decode( const unsigned char * pucSource, size_t xSourceLen, size_t * pxDestLen );

#ifdef __cplusplus
}
#endif

#endif /* BASE64_H */
