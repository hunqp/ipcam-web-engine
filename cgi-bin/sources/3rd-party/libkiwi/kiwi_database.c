#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include "kiwi_database.h"

#define PR_DB_MAGIC   (0x31424B4BU) /* "KKB1" little-endian in file */
#define PR_DB_KEYLEN  (32)
#define PR_DB_IVLEN   (12)
#define PR_DB_TAGLEN  (16)

typedef struct {
    uint32_t magic;
    uint32_t len; /* Plaintext length, also doubles as the ciphertext length */
    uint8_t iv[PR_DB_IVLEN];
    uint8_t tag[PR_DB_TAGLEN];
} FILE_DB_HEADER_T;

/**
 * Module-level context bound by Kiwi_Database_Setup(): the AES-256-GCM key derived
 * from the device private key. The key material itself is never kept, only this
 * derived key lives in memory, and only for as long as the process is up.
 */
static struct {
    uint8_t key[PR_DB_KEYLEN];
    bool configured;
} sDefaultGlobalDatabase = {0};

/**
 * Load a PEM-encoded private key file and extract its raw (DER) key material.
 * Parsing the key (instead of just slurping the file's bytes) validates that the
 * path actually points at a private key and normalizes away formatting differences
 * (line endings, whitespace) so the derived storage key stays stable across re-saves
 * of the same key. The caller owns *pp and must OPENSSL_cleanse()+OPENSSL_free() it.
 */
static int PR_ExtractPrivateKeyContent(const char *storageKeyFilename, uint8_t **pp, int *size) {
    *pp = NULL;
    *size = 0;

    BIO *bio = BIO_new_file(storageKeyFilename, "r");
    if (!bio) {
        return -1;
    }

    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) {
        return -1;
    }

    uint8_t *der = NULL;
    int len = i2d_PrivateKey(pkey, &der);
    EVP_PKEY_free(pkey);
    if (len <= 0 || !der) {
        return -1;
    }

    *pp = der;
    *size = len;
    
    return 0;
}

/**
 * Key derivation
 * HKDF-SHA256(salt, secret) -> PRK, then HKDF-Expand(PRK, info) -> 32-byte key. One expand
 * round is enough since SHA-256 already outputs 32 bytes. The salt/info labels are unique
 * to this module so the derived key never collides with kiwi_credentials's, even though
 * both may be derived from the same device private key.
 */
static void PR_AES_KEY_Derive(const uint8_t *secret, int secretLen, uint8_t key[PR_DB_KEYLEN]) {
    unsigned int prkLen = 0;
    uint8_t prk[EVP_MAX_MD_SIZE] = {0};
    static const uint8_t SALT[] = "KIWI_DATABASE_V1_SALT";
    HMAC(EVP_sha256(), SALT, sizeof(SALT) - 1, secret, secretLen, prk, &prkLen);

    static const uint8_t AES256GCM[] = "KIWI_DATABASE_V1_AES256GCM_KEY";
    uint8_t arr[sizeof(AES256GCM)];
    memcpy(arr, AES256GCM, sizeof(AES256GCM) - 1);
    arr[sizeof(AES256GCM) - 1] = 0x01; /* HKDF-Expand block counter */

    unsigned int okmLen = 0;
    uint8_t okm[EVP_MAX_MD_SIZE] = {0};
    HMAC(EVP_sha256(), prk, prkLen, arr, sizeof(arr), okm, &okmLen);
    memcpy(key, okm, PR_DB_KEYLEN);

    OPENSSL_cleanse(prk, sizeof(prk));
    OPENSSL_cleanse(okm, sizeof(okm));
}

/* ---- AES-256-GCM whole-blob encryption ---------------------------------------------------- */

static int PR_AES_GCM_Encrypt(
    const uint8_t *key, const uint8_t *iv, const uint8_t *aad, int aadLen,
    const uint8_t *plaintStr, int plaintStrLen, uint8_t *cipherStr, uint8_t *tag) {
    int result = -1;
    int cipherStrLen = 0, len = 0;

    EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
    if (!cipher) {
        return -1;
    }
    do {
        if (EVP_EncryptInit_ex(cipher, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
            break;

        if (EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, PR_DB_IVLEN, NULL) != 1)
            break;

        if (EVP_EncryptInit_ex(cipher, NULL, NULL, key, iv) != 1)
            break;

        if (EVP_EncryptUpdate(cipher, NULL, &len, aad, aadLen) != 1)
            break;

        if (EVP_EncryptUpdate(cipher, cipherStr, &len, plaintStr, plaintStrLen) != 1)
            break;

        cipherStrLen = len;

        if (EVP_EncryptFinal_ex(cipher, cipherStr + cipherStrLen, &len) != 1)
            break;

        cipherStrLen += len;

        if (EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_GET_TAG, PR_DB_TAGLEN, tag) != 1)
            break;

        result = cipherStrLen;
    }
    while (0);

    EVP_CIPHER_CTX_free(cipher);
    return result;
}

static int PR_AES_GCM_Decrypt(
    const uint8_t *key, const uint8_t *iv, const uint8_t *aad, int aadLen,
    const uint8_t *cipherStr, int cipherStrLen, const uint8_t *tag, uint8_t *plaintStr) {
    int result = -1;
    int len = 0, plaintStrLen = 0;

    EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
    if (!cipher) {
        return -1;
    }

    do {
        if (EVP_DecryptInit_ex(cipher, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
            break;

        if (EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, PR_DB_IVLEN, NULL) != 1)
            break;

        if (EVP_DecryptInit_ex(cipher, NULL, NULL, key, iv) != 1)
            break;

        if (EVP_DecryptUpdate(cipher, NULL, &len, aad, aadLen) != 1)
            break;

        if (EVP_DecryptUpdate(cipher, plaintStr, &len, cipherStr, cipherStrLen) != 1)
            break;

        plaintStrLen = len;

        if (EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, PR_DB_TAGLEN, (void *)tag) != 1)
            break;

        if (EVP_DecryptFinal_ex(cipher, plaintStr + plaintStrLen, &len) != 1)
            break; /* tag mismatch fails here */

        plaintStrLen += len;
        result = plaintStrLen;
    }
    while (0);

    EVP_CIPHER_CTX_free(cipher);
    return result;
}

/* ---- Public API ----------------------------------------------------------------------------- */

/**
 * @brief Derive this module's AES-256-GCM storage key from a device private key.
 *        Must be called before Kiwi_Database_Save()/Kiwi_Database_Read(); calling it
 *        again rebinds to a new key. If it is never called (or the key file cannot be
 *        read/parsed), Save() is a no-op and Read() always zero-fills its buffer.
 * @param[in] storageKeyFilename Path to a PEM-encoded private key file (e.g. the
 *            device's TLS server key). Its key material is extracted from this file
 *            and fed into HKDF to derive the storage key; only the derived key is
 *            kept in memory, the key material itself is never stored.
 */
void Kiwi_Database_Setup(const char *storageKeyFilename) {
    if (!storageKeyFilename) {
        return;
    }

    uint8_t *keyContent = NULL;
    int keyContentLen = 0;
    if (PR_ExtractPrivateKeyContent(storageKeyFilename, &keyContent, &keyContentLen) != 0) {
        return;
    }

    OPENSSL_cleanse(sDefaultGlobalDatabase.key, sizeof(sDefaultGlobalDatabase.key));
    PR_AES_KEY_Derive(keyContent, keyContentLen, sDefaultGlobalDatabase.key);
    sDefaultGlobalDatabase.configured = true;

    OPENSSL_cleanse(keyContent, keyContentLen);
    OPENSSL_free(keyContent);
}

/**
 * @brief Encrypt `plaintext` with AES-256-GCM and write it to `filename`, replacing
 *        any existing content. Intended for small, individually-keyed blobs such as
 *        an access token, e.g. one file per item rather than a multi-record database.
 * @param[in] filename The path of the file to write the encrypted blob into.
 * @param[in] plaintext The data to encrypt and store.
 * @param[in] len Length of `plaintext`, in bytes. Nothing is written if Setup() was
 *            not called first, or if `filename`/`plaintext` is NULL or `len` is 0.
 */
void Kiwi_Database_Save(const char *filename, uint8_t *plaintext, uint32_t len) {
    if (!sDefaultGlobalDatabase.configured || !filename || !plaintext || len == 0) {
        return;
    }

    uint8_t *cipher = (uint8_t *)malloc(len);
    if (!cipher) {
        return;
    }

    FILE_DB_HEADER_T hdr = {
        .magic = PR_DB_MAGIC,
        .len = len,
    };
    RAND_bytes(hdr.iv, sizeof(hdr.iv));

    /* Bind magic + length into the tag, so a truncated/extended file fails to decrypt */
    uint8_t aad[8];
    memcpy(aad, &hdr.magic, 4);
    memcpy(aad + 4, &hdr.len, 4);

    int n = PR_AES_GCM_Encrypt(sDefaultGlobalDatabase.key, hdr.iv, aad, sizeof(aad), plaintext, (int)len, cipher, hdr.tag);
    if (n == (int)len) {
        FILE *fp = fopen(filename, "wb");
        if (fp) {
            fwrite(&hdr, sizeof(hdr), 1, fp);
            fwrite(cipher, 1, len, fp);
            fclose(fp);
        }
    }

    OPENSSL_cleanse(cipher, len);
    free(cipher);

    system("sync");
}

/**
 * @brief Read back and decrypt the blob written by Kiwi_Database_Save().
 * @param[in] filename The path of the encrypted file to read.
 * @param[out] buffer Destination buffer receiving `len` decrypted bytes. Always
 *             zero-filled first, so a missing file, wrong/rotated key, tampered
 *             content, or a `len` that does not match what was saved leaves `buffer`
 *             all zero instead of stale or partial data.
 * @param[in] len Capacity of `buffer`, in bytes; must equal the `len` originally
 *            passed to Kiwi_Database_Save() for this file.
 */
void Kiwi_Database_Read(const char *filename, uint8_t *buffer, uint32_t len) {
    if (!buffer || len == 0) {
        return;
    }
    memset(buffer, 0, len);

    if (!sDefaultGlobalDatabase.configured || !filename) {
        return;
    }

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return; /* Not created yet */
    }

    uint8_t *cipher = NULL;
    FILE_DB_HEADER_T hdr = {0};
    do {
        if (fread(&hdr, sizeof(hdr), 1, fp) != 1 || hdr.magic != PR_DB_MAGIC) {
            break;
        }
        if (hdr.len == 0 || hdr.len != len) {
            break; /* Caller's buffer size must match what was stored */
        }
        cipher = (uint8_t *)malloc(hdr.len);
        if (!cipher || fread(cipher, 1, hdr.len, fp) != hdr.len) {
            break;
        }

        uint8_t aad[8];
        memcpy(aad, &hdr.magic, 4);
        memcpy(aad + 4, &hdr.len, 4);

        int n = PR_AES_GCM_Decrypt(sDefaultGlobalDatabase.key, hdr.iv, aad, sizeof(aad), cipher, (int)hdr.len, hdr.tag, buffer);
        if (n != (int)hdr.len) {
            memset(buffer, 0, len); /* Wrong key or tampered/corrupt file */
        }
    }
    while (0);

    if (cipher) {
        OPENSSL_cleanse(cipher, hdr.len);
        free(cipher);
    }
    fclose(fp);
}
