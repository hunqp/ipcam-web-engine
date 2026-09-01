#include "sha1.h"

#define sha1CIRCULAR_SHIFT(bits,word) (((word) << (bits)) | ((word) >> (32-(bits))))

static void ProcedureMessageBlock(Sha1Context_t *me) {
    /* Constants defined in SHA-1 */
    const uint32_t K[] = {
        0x5A827999,
        0x6ED9EBA1,
        0x8F1BBCDC,
        0xCA62C1D6
    };
    int t;
    uint32_t temp;
    uint32_t W[80] = {0};
    uint32_t A, B, C, D, E;

    for (t = 0; t < 16; t++) {
        W[t] = (uint32_t)me->MessageBlock[t * 4] << 24;
        W[t] |= (uint32_t)me->MessageBlock[t * 4 + 1] << 16;
        W[t] |= (uint32_t)me->MessageBlock[t * 4 + 2] << 8;
        W[t] |= (uint32_t)me->MessageBlock[t * 4 + 3];
    }

    for (t = 16; t < 80; t++) {
       W[t] = sha1CIRCULAR_SHIFT(1,W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);
    }

    A = me->IntermediateHash[0];
    B = me->IntermediateHash[1];
    C = me->IntermediateHash[2];
    D = me->IntermediateHash[3];
    E = me->IntermediateHash[4];

    for (t = 0; t < 20; t++) {
        temp =  sha1CIRCULAR_SHIFT(5,A) + ((B & C) | ((~B) & D)) + E + W[t] + K[0];
        E = D;
        D = C;
        C = sha1CIRCULAR_SHIFT(30,B);
        B = A;
        A = temp;
    }

    for (t = 20; t < 40; t++) {
        temp = sha1CIRCULAR_SHIFT(5,A) + (B ^ C ^ D) + E + W[t] + K[1];
        E = D;
        D = C;
        C = sha1CIRCULAR_SHIFT(30,B);
        B = A;
        A = temp;
    }

    for (t = 40; t < 60; t++) {
        temp = sha1CIRCULAR_SHIFT(5,A) +
               ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
        E = D;
        D = C;
        C = sha1CIRCULAR_SHIFT(30,B);
        B = A;
        A = temp;
    }

    for (t = 60; t < 80; t++) {
        temp = sha1CIRCULAR_SHIFT(5,A) + (B ^ C ^ D) + E + W[t] + K[3];
        E = D;
        D = C;
        C = sha1CIRCULAR_SHIFT(30,B);
        B = A;
        A = temp;
    }

    me->IntermediateHash[0] += A;
    me->IntermediateHash[1] += B;
    me->IntermediateHash[2] += C;
    me->IntermediateHash[3] += D;
    me->IntermediateHash[4] += E;
    me->MessageBlockIndex = 0;
}

static void PaddingMessages(Sha1Context_t *me) {
    if (me->MessageBlockIndex > 55) {
        me->MessageBlock[me->MessageBlockIndex++] = 0x80;
        while(me->MessageBlockIndex < 64) {
            me->MessageBlock[me->MessageBlockIndex++] = 0;
        }

        ProcedureMessageBlock(me);

        while(me->MessageBlockIndex < 56) {
            me->MessageBlock[me->MessageBlockIndex++] = 0;
        }
    }
    else {
        me->MessageBlock[me->MessageBlockIndex++] = 0x80;
        while(me->MessageBlockIndex < 56) {
            me->MessageBlock[me->MessageBlockIndex++] = 0;
        }
    }

    me->MessageBlock[56] = me->LengthOfLowBits >> 24;
    me->MessageBlock[57] = me->LengthOfLowBits >> 16;
    me->MessageBlock[58] = me->LengthOfLowBits >> 8;
    me->MessageBlock[59] = me->LengthOfLowBits;
    me->MessageBlock[60] = me->LengthOfHighBits >> 24;
    me->MessageBlock[61] = me->LengthOfHighBits >> 16;
    me->MessageBlock[62] = me->LengthOfHighBits >> 8;
    me->MessageBlock[63] = me->LengthOfHighBits;

    ProcedureMessageBlock(me);
}

int Sha1Reset(Sha1Context_t *me) {
    if (!me) {
        return sha1NULL;
    }

    me->LengthOfHighBits = 0;
    me->LengthOfLowBits = 0;
    me->MessageBlockIndex = 0;

    me->IntermediateHash[0] = 0x67452301;
    me->IntermediateHash[1] = 0xEFCDAB89;
    me->IntermediateHash[2] = 0x98BADCFE;
    me->IntermediateHash[3] = 0x10325476;
    me->IntermediateHash[4] = 0xC3D2E1F0;

    me->IsDigestComputed = 0;
    me->IsDigestCorrupted = 0;

    return sha1SUCCESS;
}

int Sha1Result(Sha1Context_t *me, uint8_t messageDigest[sha1HASH_SIZE]) {
    int i;

    if (!me || !messageDigest) {
        return sha1NULL;
    }

    if (me->IsDigestCorrupted) {
        return me->IsDigestCorrupted;
    }

    if (!me->IsDigestComputed) {
        PaddingMessages(me);
        for (i = 0; i < 64; ++i) {
            me->MessageBlock[i] = 0;
        }
        me->LengthOfHighBits = 0;
        me->LengthOfLowBits = 0;
        me->IsDigestComputed = 1;
    }

    for(i = 0; i < sha1HASH_SIZE; ++i) {
        messageDigest[i] = me->IntermediateHash[i >> 2] >> 8 * (3 - (i & 0x03));
    }

    return sha1SUCCESS;
}

int Sha1Input(Sha1Context_t *me, const uint8_t *message, unsigned size) {
    if (!size) {
        return sha1SUCCESS;
    }

    if (!me || !message) {
        return sha1NULL;
    }

    if (me->IsDigestComputed) {
        me->IsDigestCorrupted = sha1STATE_ERROR;
        return sha1STATE_ERROR;
    }

    if (me->IsDigestCorrupted) {
        return me->IsDigestCorrupted;
    }
    while(size-- && !me->IsDigestCorrupted) {
        me->MessageBlock[me->MessageBlockIndex++] = (*message & 0xFF);

        me->LengthOfHighBits += 8;
        if (me->LengthOfHighBits == 0) {
            me->LengthOfLowBits++;
            if (me->LengthOfLowBits == 0) {
                me->IsDigestCorrupted = 1;
            }
        }

        if (me->MessageBlockIndex == 64) {
            ProcedureMessageBlock(me);
        }

        message++;
    }

    return sha1SUCCESS;
}
