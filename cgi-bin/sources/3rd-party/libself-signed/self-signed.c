#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "cgi_debug.h"
#include "self-signed.h"

#if !defined(USE_OPENSSL) && !defined(USE_MBEDTLS)
#define USE_OPENSSL
#endif

#if defined(USE_OPENSSL) && defined(USE_MBEDTLS)
#error "Only one of USE_OPENSSL or USE_MBEDTLS can be defined"
#endif

#ifdef USE_OPENSSL
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#endif

#ifdef USE_MBEDTLS
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#endif

/* Longest decimal string that safely fits in a signed 64-bit value. */
#define MAX_EXPIRY_DIGITS (19)
/* Self-sign certificate placement in the filesystem */
#define CERTIFICATE_FILE  (const char*)"/oem/usr/etc/init.d/firmware.crt"

/*---------------------------------------------------------------------------------------------------------------*/
/* Small string helpers. All operate on pointer+length slices instead of allocating, since every caller here     */
/* already owns a buffer (the token, or a decoded payload) that outlives the slice.                              */
/*---------------------------------------------------------------------------------------------------------------*/

/* Trim leading/trailing whitespace without copying; outStart/outLen (by pointer) describe the trimmed slice. */
static void trimSpace(const char *value, size_t len, const char **outStart, size_t *outLen) {
    size_t start = 0;
    size_t end = len;

    while (start < end && isspace((unsigned char)value[start])) {
        start++;
    }

    while (end > start && isspace((unsigned char)value[end - 1])) {
        end--;
    }

    *outStart = value + start;
    *outLen = end - start;
}

/* Case-insensitive fixed-length compare. Only used on already-validated 32-char hex strings. */
static bool md5EqualsCaseInsensitive(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }

    return true;
}

static bool isValidMd5(const char *md5, size_t len) {
    if (len != 32) {
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        if (!isxdigit((unsigned char)md5[i])) {
            return false;
        }
    }

    return true;
}

/* Parses a decimal, NUL-free slice into a time_t. Rejects anything non-numeric, out of range, or <= 0. */
static bool parseExpiry(const char *expiryString, size_t len, time_t *expiry) {
    char buffer[MAX_EXPIRY_DIGITS + 1];

    if (len == 0 || len > MAX_EXPIRY_DIGITS) {
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)expiryString[i])) {
            return false;
        }
    }

    memcpy(buffer, expiryString, len);
    buffer[len] = '\0';

    errno = 0;
    char *end = NULL;
    long long value = strtoll(buffer, &end, 10);

    if (errno == ERANGE || end == buffer || *end != '\0' || value <= 0) {
        return false;
    }

    *expiry = (time_t)value;

    if ((long long)*expiry != value) {
        return false;
    }

    return true;
}

/* Finds a single occurrence of `sep` in value[0..len). Returns (size_t)-1 if absent or if it appears more than
 * once. Mirrors the "reject multiple separators" checks from the original code in one pass. */
static size_t findSingleSeparator(const char *value, size_t len, char sep) {
    size_t position = (size_t)-1;

    for (size_t i = 0; i < len; ++i) {
        if (value[i] == sep) {
            if (position != (size_t)-1) {
                return (size_t)-2; /* signals "found more than one" */
            }
            position = i;
        }
    }

    return position;
}

/*---------------------------------------------------------------------------------------------------------------*/
/* Base64                                                                                                         */
/*---------------------------------------------------------------------------------------------------------------*/

static unsigned char *selfBase64Encode(const unsigned char *src, size_t srcLen, size_t *dstLen) {
    if (dstLen == NULL) {
        return NULL;
    }

    *dstLen = 0;

    if (src == NULL && srcLen != 0) {
        return NULL;
    }

#ifdef USE_OPENSSL
    if (srcLen > ((size_t)INT_MAX / 4) * 3) {
        return NULL;
    }

    int encodedLength = 4 * (((int)srcLen + 2) / 3);

    unsigned char *output = malloc((size_t)encodedLength + 1);
    if (output == NULL) {
        return NULL;
    }

    int result = EVP_EncodeBlock(output, src, (int)srcLen);
    if (result < 0) {
        free(output);
        return NULL;
    }

    output[result] = '\0';
    *dstLen = (size_t)result;

    return output;
#endif

#ifdef USE_MBEDTLS
    size_t requiredLength = 0;

    int result = mbedtls_base64_encode(NULL, 0, &requiredLength, src, srcLen);
    if (result != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && result != 0) {
        return NULL;
    }

    unsigned char *output = malloc(requiredLength + 1);
    if (output == NULL) {
        return NULL;
    }

    size_t encodedLength = 0;

    result = mbedtls_base64_encode(output, requiredLength, &encodedLength, src, srcLen);
    if (result != 0) {
        free(output);
        return NULL;
    }

    output[encodedLength] = '\0';
    *dstLen = encodedLength;

    return output;
#endif
}

static unsigned char *selfBase64Decode(const unsigned char *src, size_t srcLen, size_t *dstLen) {
    if (dstLen == NULL) {
        return NULL;
    }

    *dstLen = 0;

    if (src == NULL || srcLen == 0) {
        return NULL;
    }

#ifdef USE_OPENSSL
    if ((srcLen % 4) != 0 || srcLen > (size_t)INT_MAX) {
        return NULL;
    }

    size_t allocationSize = (srcLen / 4) * 3;

    unsigned char *output = malloc(allocationSize + 1);
    if (output == NULL) {
        return NULL;
    }

    int decodedLength = EVP_DecodeBlock(output, src, (int)srcLen);
    if (decodedLength < 0) {
        free(output);
        return NULL;
    }

    if (srcLen >= 1 && src[srcLen - 1] == '=') {
        --decodedLength;
    }

    if (srcLen >= 2 && src[srcLen - 2] == '=') {
        --decodedLength;
    }

    if (decodedLength < 0) {
        free(output);
        return NULL;
    }

    output[decodedLength] = '\0';
    *dstLen = (size_t)decodedLength;

    return output;
#endif

#ifdef USE_MBEDTLS
    size_t requiredLength = 0;

    int result = mbedtls_base64_decode(NULL, 0, &requiredLength, src, srcLen);
    if (result != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && result != 0) {
        return NULL;
    }

    unsigned char *output = malloc(requiredLength + 1);
    if (output == NULL) {
        return NULL;
    }

    size_t decodedLength = 0;

    result = mbedtls_base64_decode(output, requiredLength, &decodedLength, src, srcLen);
    if (result != 0) {
        free(output);
        return NULL;
    }

    output[decodedLength] = '\0';
    *dstLen = decodedLength;

    return output;
#endif
}

/*---------------------------------------------------------------------------------------------------------------*/
/* Certificate loading (mbedTLS only; OpenSSL reads directly via stdio + PEM_read_X509)                          */
/*---------------------------------------------------------------------------------------------------------------*/

#ifdef USE_MBEDTLS
/* Reads the whole file into a malloc'd, NUL-terminated buffer (mbedtls_x509_crt_parse requires the NUL when fed
 * PEM data, and expects it counted in the supplied length). */
static bool readFile(const char *filename, unsigned char **content, size_t *contentLen) {
    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        fprintf(stderr, "Cannot open certificate: %s\n", filename);
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }

    long fileSize = ftell(fp);

    if (fileSize <= 0) {
        fclose(fp);
        return false;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    unsigned char *buffer = malloc((size_t)fileSize + 1);
    if (buffer == NULL) {
        fclose(fp);
        return false;
    }

    size_t bytesRead = fread(buffer, 1, (size_t)fileSize, fp);
    fclose(fp);

    if (bytesRead != (size_t)fileSize) {
        free(buffer);
        return false;
    }

    buffer[fileSize] = '\0';
    *content = buffer;
    *contentLen = (size_t)fileSize + 1; /* include the NUL, as mbedtls_x509_crt_parse expects for PEM input */

    return true;
}
#endif

/*---------------------------------------------------------------------------------------------------------------*/
/* Signature verification                                                                                        */
/*---------------------------------------------------------------------------------------------------------------*/

bool verifySignature(
    const char *payload, size_t payloadLen,
    const unsigned char *signature, size_t signatureLen,
    const char *certFile) {
    /**/
    if (payload == NULL || payloadLen == 0 || signature == NULL || signatureLen == 0 || certFile == NULL) {
        return false;
    }

#ifdef USE_OPENSSL
    FILE *fp = fopen(certFile, "rb");

    if (fp == NULL) {
        fprintf(stderr, "Cannot open certificate: %s\n", certFile);
        return false;
    }

    X509 *certificate = PEM_read_X509(fp, NULL, NULL, NULL);
    fclose(fp);

    if (certificate == NULL) {
        fprintf(stderr, "Cannot parse X.509 certificate\n");
        ERR_print_errors_fp(stderr);
        return false;
    }

    EVP_PKEY *publicKey = X509_get_pubkey(certificate);
    X509_free(certificate);

    if (publicKey == NULL) {
        fprintf(stderr, "Cannot extract public key\n");
        ERR_print_errors_fp(stderr);
        return false;
    }

    EVP_MD_CTX *context = EVP_MD_CTX_new();

    if (context == NULL) {
        fprintf(stderr, "Cannot create OpenSSL context\n");
        EVP_PKEY_free(publicKey);
        return false;
    }

    bool valid = false;

    if (EVP_DigestVerifyInit(context, NULL, EVP_sha256(), NULL, publicKey) != 1) {
        fprintf(stderr, "EVP_DigestVerifyInit failed\n");
        ERR_print_errors_fp(stderr);
    } else if (EVP_DigestVerifyUpdate(context, payload, payloadLen) != 1) {
        fprintf(stderr, "EVP_DigestVerifyUpdate failed\n");
        ERR_print_errors_fp(stderr);
    } else {
        int result = EVP_DigestVerifyFinal(context, signature, signatureLen);

        if (result == 1) {
            valid = true;
        } else if (result == 0) {
            fprintf(stderr, "Signature verification failed\n");
        } else {
            fprintf(stderr, "OpenSSL verification error\n");
            ERR_print_errors_fp(stderr);
        }
    }

    EVP_MD_CTX_free(context);
    EVP_PKEY_free(publicKey);

    return valid;
#endif

#ifdef USE_MBEDTLS
    unsigned char *certificateData = NULL;
    size_t certificateDataLen = 0;

    if (!readFile(certFile, &certificateData, &certificateDataLen)) {
        return false;
    }

    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);

    int result = mbedtls_x509_crt_parse(&certificate, certificateData, certificateDataLen);
    free(certificateData);

    if (result != 0) {
        fprintf(stderr, "Cannot parse X.509 certificate: -0x%04X\n", (unsigned int)(-result));
        mbedtls_x509_crt_free(&certificate);
        return false;
    }

    const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (mdInfo == NULL) {
        fprintf(stderr, "SHA-256 is unavailable\n");
        mbedtls_x509_crt_free(&certificate);
        return false;
    }

    unsigned char hash[32];

    result = mbedtls_md(mdInfo, (const unsigned char *)payload, payloadLen, hash);

    if (result != 0) {
        fprintf(stderr, "Cannot calculate SHA-256: -0x%04X\n", (unsigned int)(-result));
        mbedtls_x509_crt_free(&certificate);
        return false;
    }

    result = mbedtls_pk_verify(&certificate.pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), signature, signatureLen);

    mbedtls_x509_crt_free(&certificate);

    if (result != 0) {
        fprintf(stderr, "Signature verification failed: -0x%04X\n", (unsigned int)(-result));
        return false;
    }

    return true;
#endif
}

/*---------------------------------------------------------------------------------------------------------------*/
/* Token validation                                                                                               */
/*---------------------------------------------------------------------------------------------------------------*/

bool validateSecret(const char *secret, char *md5sum, char **signature, char **errorStr) {
    if (secret == NULL) {
        return false;
    }

    size_t tokenLen = 0;
    const char *token = NULL;
    trimSpace(secret, strlen(secret), &token, &tokenLen);

    const char *expectedMd5 = NULL;
    size_t expectedMd5Len = 0;
    trimSpace(md5sum, strlen(md5sum), &expectedMd5, &expectedMd5Len);

    if (tokenLen == 0) {
        *errorStr = (const char*)strdup("Secret token is empty");
        return false;
    }

    if (!isValidMd5(expectedMd5, expectedMd5Len)) {
        *errorStr = (const char*)strdup("Invalid expected MD5 format");
        return false;
    }

    size_t dotPosition = findSingleSeparator(token, tokenLen, '.');

    if (dotPosition == (size_t)-2) {
        *errorStr = (const char*)strdup("Secret token contains multiple separators");
        return false;
    }

    if (dotPosition == (size_t)-1 || dotPosition == 0 || dotPosition + 1 >= tokenLen) {
        *errorStr = (const char*)strdup("Invalid secret token format");
        return false;
    }

    const char *payloadBase64 = token;
    size_t payloadBase64Len = dotPosition;
    const char *signatureBase64 = token + dotPosition + 1;
    size_t signatureBase64Len = tokenLen - dotPosition - 1;

    if (signature) {
        *signature = malloc(signatureBase64Len + 1);
        if (*signature) {
            memcpy(*signature, signatureBase64, signatureBase64Len);
            (*signature)[signatureBase64Len] = '\0';
        }
    }

    /* Decode payload and signature (payload.signature) */

    size_t payloadLen = 0;
    unsigned char *payloadBuffer = selfBase64Decode((const unsigned char *)payloadBase64, payloadBase64Len, &payloadLen);

    if (payloadBuffer == NULL || payloadLen == 0) {
        *errorStr = (const char*)strdup("Cannot decode payload");
        free(payloadBuffer);
        return false;
    }

    size_t signatureLen = 0;
    unsigned char *signatureBuffer = selfBase64Decode((const unsigned char *)signatureBase64, signatureBase64Len, &signatureLen);

    if (signatureBuffer == NULL || signatureLen == 0) {
        *errorStr = (const char*)strdup("Cannot decode signature");
        free(payloadBuffer);
        free(signatureBuffer);
        return false;
    }

    bool valid = verifySignature((const char *)payloadBuffer, payloadLen, signatureBuffer, signatureLen, CERTIFICATE_FILE);
    free(signatureBuffer);
    if (!valid) {
        *errorStr = (const char*)strdup("Invalid signature");
        free(payloadBuffer);
        return false;
    }

    /* Extract MD5 and expiration (MD5|expiration) */

    size_t separatorPosition = findSingleSeparator((const char *)payloadBuffer, payloadLen, '|');

    if (separatorPosition == (size_t)-2) {
        *errorStr = (const char*)strdup("Signed payload contains multiple separators");
        free(payloadBuffer);
        return false;
    }

    if (separatorPosition == (size_t)-1 || separatorPosition == 0 || separatorPosition + 1 >= payloadLen) {
        *errorStr = (const char*)strdup("Invalid signed payload format");
        free(payloadBuffer);
        return false;
    }

    const char *signedMd5 = (const char *)payloadBuffer;
    size_t signedMd5Len = separatorPosition;
    const char *expiryString = (const char *)payloadBuffer + separatorPosition + 1;
    size_t expiryStringLen = payloadLen - separatorPosition - 1;

    if (!isValidMd5(signedMd5, signedMd5Len)) {
        *errorStr = (const char*)strdup("Invalid signed MD5 format");
        free(payloadBuffer);
        return false;
    }

    time_t expiry = 0;

    if (!parseExpiry(expiryString, expiryStringLen, &expiry)) {
        *errorStr = (const char*)strdup("Invalid expiration timestamp");
        free(payloadBuffer);
        return false;
    }

    free(payloadBuffer); /* Nothing further reads the decoded payload */

    if (time(NULL) >= expiry) {
        *errorStr = (const char*)strdup("Secret token has expired");
        return false;
    }

    return true;
}
