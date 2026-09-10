/*
 * sha1.h
 *
 * Minimal SHA-1 implementation, restyled to the FreeRTOS coding
 * convention: public functions named "<return-type prefix>Sha1<Verb>",
 * Hungarian-notation struct fields, "prv"-prefixed private helpers in
 * sha1.c, Allman braces.
 */

#ifndef SHA1_H
#define SHA1_H

#include <stdint.h>

enum
{
    sha1SUCCESS = 0,
    sha1NULL,
    sha1INPUT_TOO_LONG,
    sha1STATE_ERROR
};

#define sha1HASH_SIZE    20

/* Sha1Context_t: running state of one SHA-1 computation. Reset with
 * xSha1Reset(), fed with one or more calls to xSha1Input(), and read
 * back with xSha1Result(). */
typedef struct
{
    /* Message digest ( 5 x 32-bit words ). */
    uint32_t ulIntermediateHash[ sha1HASH_SIZE / 4 ];

    /* Message length, in bits, split across two 32-bit halves. */
    uint32_t ulLengthHighBits;
    uint32_t ulLengthLowBits;

    /* Write cursor into ucMessageBlock. */
    int_least16_t sMessageBlockIndex;

    /* Current 512-bit ( 64-byte ) message block being assembled. */
    uint8_t ucMessageBlock[ 64 ];

    /* Non-zero once xSha1Result() has computed the final digest. */
    int xIsDigestComputed;

    /* Non-zero once too much input has made the digest unusable
     * ( see sha1STATE_ERROR ). */
    int xIsDigestCorrupted;
} Sha1Context_t;

/**
 * xSha1Reset
 *
 * Resets a SHA-1 context to its initial state, ready for xSha1Input().
 *
 * @param pxContext Pointer to the SHA-1 context to reset.
 * @return sha1SUCCESS on success, sha1NULL if pxContext is NULL.
 */
extern int xSha1Reset( Sha1Context_t * pxContext );

/**
 * xSha1Input
 *
 * Feeds ulSize bytes of pucMessage into the running SHA-1 computation.
 * May be called multiple times to hash data incrementally.
 *
 * @param pxContext Pointer to the SHA-1 context.
 * @param pucMessage Pointer to the input message buffer.
 * @param ulSize     Size of the input message, in bytes.
 * @return sha1SUCCESS on success, sha1NULL or sha1STATE_ERROR on failure.
 */
extern int xSha1Input( Sha1Context_t * pxContext, const uint8_t * pucMessage, unsigned ulSize );

/**
 * xSha1Result
 *
 * Finalises the SHA-1 computation and writes out the message digest.
 * After this call the context must be reset before it can be reused.
 *
 * @param pxContext      Pointer to the SHA-1 context.
 * @param pucMessageDigest Output buffer, must be sha1HASH_SIZE bytes.
 * @return sha1SUCCESS on success, sha1NULL on a NULL argument, or the
 *         corruption code recorded earlier by xSha1Input().
 */
extern int xSha1Result( Sha1Context_t * pxContext, uint8_t pucMessageDigest[ sha1HASH_SIZE ] );

#endif /* SHA1_H */
