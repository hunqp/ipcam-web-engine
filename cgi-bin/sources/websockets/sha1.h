#ifndef SHA1_H
#define SHA1_H

#include <stdint.h>

enum {
    sha1SUCCESS = 0,
    sha1NULL,
    sha1INPUT_TOO_LONG,
    sha1STATE_ERROR
};

#define sha1HASH_SIZE 20

typedef struct {
    /* Message Digest */
    uint32_t IntermediateHash[sha1HASH_SIZE / 4];

    /* Message length in bits */
    uint32_t LengthOfHighBits;
    uint32_t LengthOfLowBits; 

    /* Index into message block array */
    int_least16_t MessageBlockIndex;
    /* 512-bit message blocks */
    uint8_t MessageBlock[64];

    /* Is the digest computed */
    int IsDigestComputed;
    /* Is the message digest corrupted? */
    int IsDigestCorrupted;
} Sha1Context_t;

/**
 * Reset SHA1 context State.
 *
 * \param[in,out] me Pointer to SHA1 context.
 * \return 0 on success, -1 on failure.
 */
extern int Sha1Reset(Sha1Context_t *me);

/**
 * Feed input data into SHA1 context.
 *
 * \param[in,out] me Pointer to SHA1 context.
 * \param[in] message Pointer to input message buffer.
 * \param[in] size Size of input message in bytes.
 * \return 0 on success, -1 on failure.
 */
extern int Sha1Input(Sha1Context_t *me, const uint8_t *message, unsigned size);

/**
 * Finalize SHA1 calculation and get message digest.
 *
 * \param[in,out] me Pointer to SHA1 context.
 * \param[out] messageDigest Output SHA1 digest buffer.
 *                           Must be sha1HASH_SIZE bytes.
 * \return 0 on success, -1 on failure.
 */
extern int Sha1Result(Sha1Context_t *me, uint8_t messageDigest[sha1HASH_SIZE]);

#endif /* SHA1_H */
