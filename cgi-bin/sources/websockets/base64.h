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
 * Encode binary data into Base64 string.
 *
 * \param[in] src Pointer to source binary buffer.
 * \param[in] srcLen Length of source buffer in bytes.
 * \param[out] dstLen Pointer to encoded output length.
 * \return Pointer to allocated Base64 encoded buffer,
 *         or NULL on failure.
 *
 * \note Caller is responsible for freeing returned buffer.
 */
unsigned char * Base64Encode(const unsigned char *src, size_t srcLen, size_t *dstLen);

/**
 * Decode Base64 string into binary data.
 *
 * \param[in] src Pointer to Base64 encoded buffer.
 * \param[in] srcLen Length of Base64 buffer in bytes.
 * \param[out] dstLen Pointer to decoded output length.
 * \return Pointer to allocated decoded buffer,
 *         or NULL on failure.
 *
 * \note Caller is responsible for freeing returned buffer.
 */
unsigned char * Base64Decode(const unsigned char *src, size_t srcLen, size_t *dstLen);

#ifdef __cplusplus
}
#endif

#endif /* BASE64_H */
