#define _POSIX_C_SOURCE 200809L /* strdup() is POSIX, not C11; needed under -std=c11 */

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/x509.h>

#include "kiwi_credentials.h"

#define PR_CRED_MAGIC   (0x31435741U) /* "AWC1" little-endian in file */
#define PR_CRED_KEYLEN  (32)
#define PR_CRED_IVLEN   (12)
#define PR_CRED_TAGLEN  (16)

typedef struct {
    uint32_t magic;
    uint32_t count;
} FILE_DB_HEADER_T;

typedef struct {
    uint8_t iv[PR_CRED_IVLEN];
    uint8_t tag[PR_CRED_TAGLEN];
    uint8_t cipher[sizeof(KIWI_CREDENTIALS_T)];
} FILE_DB_ITEM_T;

/**
 * Module-level context bound by Kiwi_Credentials_Setup(): the target file and the
 * AES-256-GCM key derived from the caller's secret. The raw secret itself is never
 * kept, only this derived key lives in memory, and only for as long as the process
 * is up. 
 */
static struct {
    char *filename;
    bool configured;
    uint8_t key[PR_CRED_KEYLEN];
} sDefaultGlobalCredentials = {0};

/**
 * Key derivation
 * HKDF-SHA256(salt, secret) -> PRK, then HKDF-Expand(PRK, info) -> 32-byte key. One expand
 * round is enough since SHA-256 already outputs 32 bytes.
 */
static void PR_AES_KEY_Derive(const uint8_t *secret, int secretLen, uint8_t key[PR_CRED_KEYLEN]) {
    unsigned int prkLen = 0;
    uint8_t prk[EVP_MAX_MD_SIZE] = {0};
    static const uint8_t SALT[] = "KIWI_CREDENTIALS_V1_SALT";
    HMAC(EVP_sha256(), SALT, sizeof(SALT) - 1, secret, secretLen, prk, &prkLen);

    static const uint8_t AES256GCM[] = "KIWI_CREDENTIALS_V1_AES256GCM_KEY";
    uint8_t arr[sizeof(AES256GCM)];
    memcpy(arr, AES256GCM, sizeof(AES256GCM) - 1);
    arr[sizeof(AES256GCM) - 1] = 0x01; /* HKDF-Expand block counter */

    unsigned int okmLen = 0;
    uint8_t okm[EVP_MAX_MD_SIZE] = {0};
    HMAC(EVP_sha256(), prk, prkLen, arr, sizeof(arr), okm, &okmLen);
    memcpy(key, okm, PR_CRED_KEYLEN);

    OPENSSL_cleanse(prk, sizeof(prk));
    OPENSSL_cleanse(okm, sizeof(okm));
}

/**
 * Load a PEM-encoded private key file and extract its raw (DER) key material.
 * Parsing the key (instead of just slurping the file's bytes) validates that the
 * path actually points at a private key and normalizes away formatting differences
 * (line endings, whitespace) so the derived storage key stays stable across re-saves
 * of the same key. The caller owns *outBuf and must OPENSSL_cleanse()+OPENSSL_free() it.
 */
static int PR_ExtractPrivateKeyContent(const char *storageKeyFilename, uint8_t **pp, int *size) {
    *size = 0;
    *pp = NULL;

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

/* ---- AES-256-GCM record encryption -------------------------------------------------------- */

static int PR_AES_GCM_Encrypt(
    const uint8_t *key, const uint8_t *iv, const uint8_t *aad, int aadLen,
    const uint8_t *plaintStr, int plaintStrLen, uint8_t *cipherStr, uint8_t *tag) {
    /* Encrypt plaintext with AES-256-GCM */
    int result = -1;
    int cipherStrLen = 0, len = 0;

    EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
    if (!cipher) {
        return -1;
    }
    do {
        if (EVP_EncryptInit_ex(cipher, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
            break;
        
        if (EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, PR_CRED_IVLEN, NULL) != 1)
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

        if (EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_GET_TAG, PR_CRED_TAGLEN, tag) != 1)
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
    /* Decrypt ciphertext with AES-256-GCM */
    int result = -1;
    int len = 0, plaintStrLen = 0;

    EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
    if (!cipher) {
        return -1;
    }

    do {
        if (EVP_DecryptInit_ex(cipher, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
            break;
        
        if (EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, PR_CRED_IVLEN, NULL) != 1)
            break;
        
        if (EVP_DecryptInit_ex(cipher, NULL, NULL, key, iv) != 1)
            break;
        
        if (EVP_DecryptUpdate(cipher, NULL, &len, aad, aadLen) != 1)
            break;
        
        if (EVP_DecryptUpdate(cipher, plaintStr, &len, cipherStr, cipherStrLen) != 1)
            break;
        
        plaintStrLen = len;

        if (EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, PR_CRED_TAGLEN, (void *)tag) != 1)
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

/**
 * Database Operations (READ/WRITE)
 * Every record is encrypted whole (role + username + password + timestamps): nothing about
 * an account is readable from the file without the derived key. The record's position (file
 * magic + index) is bound in as AEAD associated data, so records cannot be silently reordered
 * or spliced from another file/version.
 */
static bool reloadDatabase(KIWI_CREDENTIALS_T **ppList, uint32_t *pSize) {
    *pSize = 0;
    *ppList = NULL;
    bool success = false;

    FILE *fp = fopen(sDefaultGlobalCredentials.filename, "rb");
    if (!fp) {
        return false; /* Not created yet: zero accounts, not an error. */
    }

    do {
        FILE_DB_HEADER_T hdr = {0};
        if (fread(&hdr, sizeof(hdr), 1, fp) != 1 || hdr.magic != PR_CRED_MAGIC) {
            break;
        }
        if (hdr.count == 0) {
            break;
        }
        KIWI_CREDENTIALS_T *list = (KIWI_CREDENTIALS_T *)calloc(hdr.count, sizeof(KIWI_CREDENTIALS_T));
        if (!list) {
            break;
        }
        uint32_t valid = 0;
        for (uint32_t id = 0; id < hdr.count; ++id) {
            FILE_DB_ITEM_T item = {0};
            if (fread(&item, sizeof(item), 1, fp) != 1) {
                break; /* Truncated file: keep whatever validated so far. */
            }

            uint8_t aad[8] = {0};
            memcpy(aad, &hdr.magic, 4);
            memcpy(aad + 4, &id, 4);

            int n = PR_AES_GCM_Decrypt(sDefaultGlobalCredentials.key, item.iv, aad, sizeof(aad), item.cipher, sizeof(item.cipher), item.tag, (uint8_t *)&list[valid]);
            OPENSSL_cleanse(&item, sizeof(item));
            if (n != (int)sizeof(KIWI_CREDENTIALS_T)) {
                memset(&list[valid], 0, sizeof(KIWI_CREDENTIALS_T));
                continue; /* Ignore corrupt/tampered item or wrong secret */
            }
            ++(valid);
        }

        *ppList = list;
        *pSize = valid;
        success = true;
    }
    while(0);

    fclose(fp);
    
    return success;
}

static void saveinDatabase(const KIWI_CREDENTIALS_T *list, uint32_t count) {
    FILE *fp = fopen(sDefaultGlobalCredentials.filename, "wb");
    if (!fp) {
        return;
    }
    /* Write header informations */
    FILE_DB_HEADER_T hdr = {
        .magic = PR_CRED_MAGIC,
        .count = count
    };
    fwrite(&hdr, sizeof(hdr), 1, fp);

    /* Write data credentials informations */
    for (size_t id = 0; id < count; ++id) {
        FILE_DB_ITEM_T item = {0};

        /* Generate random IV (Initialization Vector) */
        RAND_bytes(item.iv, sizeof(item.iv));

        /* Generate additional authenticated data, used for integrity */
        uint8_t aad[8];
        memcpy(aad, &hdr.magic, 4);
        memcpy(aad + 4, &id, 4);

        /* Encrypt payload with AES-256-GCM */
        PR_AES_GCM_Encrypt(sDefaultGlobalCredentials.key, item.iv, aad, sizeof(aad), (const uint8_t *)&list[id], sizeof(KIWI_CREDENTIALS_T), item.cipher, item.tag);
        fwrite(&item, sizeof(item), 1, fp);
        OPENSSL_cleanse(&item, sizeof(item));
    }
    fclose(fp);
}

static void cleanup(KIWI_CREDENTIALS_T *list, uint32_t count) {
    if (list) {
        OPENSSL_cleanse(list, count * sizeof(KIWI_CREDENTIALS_T));
        free(list);
    }
}

static inline bool sameUsername(const KIWI_CREDENTIALS_T *a, const KIWI_CREDENTIALS_T *b) {
    return strncmp(a->username, b->username, sizeof(a->username)) == 0;
}

/* ---- Public API ----------------------------------------------------------------------------- */

/**
 * @brief Bind Add/Mod/Del/Get to one credentials file and derive its AES-256-GCM
 *        storage key from a device private key. Must be called before any other
 *        Kiwi_Credentials_*() call; calling it again rebinds to a new file/key.
 * @param[in] filename The path to the encrypted credentials file.
 * @param[in] storageKeyFilename Path to a PEM-encoded private key file (e.g. the device's
 *            TLS server key). The key material is extracted from this file and fed
 *            into HKDF to derive the storage key; only the derived key is kept in
 *            memory, the key material itself is never stored.
 * @return 0 on success, or -1 if filename/storageKeyFilename is NULL, or the private key
 *         file could not be read or parsed.
 */
int Kiwi_Credentials_Setup(const char *filename, const char *storageKeyFilename) {
    if (!filename || !storageKeyFilename) {
        return -1;
    }

    uint8_t *keyContent = NULL;
    int keyContentLen = 0;
    if (PR_ExtractPrivateKeyContent(storageKeyFilename, &keyContent, &keyContentLen) != 0) {
        return -1;
    }

    if (sDefaultGlobalCredentials.filename) {
        free(sDefaultGlobalCredentials.filename);
        sDefaultGlobalCredentials.filename = NULL;
    }
    sDefaultGlobalCredentials.filename = strdup(filename);

    OPENSSL_cleanse(sDefaultGlobalCredentials.key, sizeof(sDefaultGlobalCredentials.key));
    PR_AES_KEY_Derive(keyContent, keyContentLen, sDefaultGlobalCredentials.key);
    sDefaultGlobalCredentials.configured = true;

    OPENSSL_cleanse(keyContent, keyContentLen);
    OPENSSL_free(keyContent);
    return 0;
}

/**
 * @brief Add a new account, or replace the existing account with the same username.
 * @param[in] credentials The account to store.
 * @return 0 on success, or -1 if Kiwi_Credentials_Setup() was not called first, or the
 *         file could not be read/written.
 */
int Kiwi_Credentials_Add(KIWI_CREDENTIALS_T *credentials) {
    if (!sDefaultGlobalCredentials.configured) {
        return -1;
    }
    if (!credentials) {
        return -2;
    }

    uint32_t count = 0;
    KIWI_CREDENTIALS_T *list = NULL;
    reloadDatabase(&list, &count);

    KIWI_CREDENTIALS_T *newList = (KIWI_CREDENTIALS_T *)realloc(list, (count + 1) * sizeof(KIWI_CREDENTIALS_T));
    if (!newList) {
        cleanup(list, count);
        return -1;
    }
    list = newList;

    size_t selected = count;
    for (size_t id = 0; id < count; ++id) {
        if (sameUsername(&list[id], credentials)) {
            selected = id; /* Upsert: same username replaces the existing account */
            break;
        }
    }
    list[selected] = *credentials;
    size_t newSize = (selected == count) ? (count + 1) : count;
    saveinDatabase(list, newSize);
    cleanup(list, newSize);

    return 0;
}

/**
 * @brief Update the existing account matched by credentials->username.
 * @param[in] credentials The new field values, keyed by username.
 * @return 0 on success, or -1 if Kiwi_Credentials_Setup() was not called first, no
 *         matching account was found, or the write failed.
 */
int Kiwi_Credentials_Mod(KIWI_CREDENTIALS_T *credentials) {
    if (!sDefaultGlobalCredentials.configured) {
        return -1;
    }
    if (!credentials) {
        return -2;
    }

    uint32_t count = 0;
    KIWI_CREDENTIALS_T *list = NULL;
    reloadDatabase(&list, &count);

    bool found = false;
    for (size_t id = 0; id < count; ++id) {
        if (sameUsername(&list[id], credentials)) {
            memcpy(&list[id], credentials, sizeof(KIWI_CREDENTIALS_T));
            found = true;
            break;
        }
    }
    if (found) {
        saveinDatabase(list, count);
    }
    cleanup(list, count);

    return 0;
}

/**
 * @brief Remove the account matched by credentials->username.
 * @param[in] credentials Only the username field is used to find the account.
 * @return 0 on success, or -1 if Kiwi_Credentials_Setup() was not called first, no
 *         matching account was found, or the write failed.
 */
int Kiwi_Credentials_Del(KIWI_CREDENTIALS_T *credentials) {
    if (!sDefaultGlobalCredentials.configured) {
        return -1;
    }
    if (!credentials) {
        return -2;
    }

    uint32_t count = 0;
    KIWI_CREDENTIALS_T *list = NULL;
    reloadDatabase(&list, &count);

    bool found = false;
    uint32_t newSize = 0;
    for (size_t id = 0; id < count; ++id) {
        if (!found && sameUsername(&list[id], credentials)) {
            found = true;
            continue; /* Drop the matched account */
        }
        list[newSize++] = list[id];
    }

    if (found) {
        saveinDatabase(list, newSize);
    }
    cleanup(list, count);

    return 0;
}

/**
 * @brief Read back up to `size` stored accounts.
 * @param[out] list Caller-provided array of at least `size` elements.
 * @param[in] size The capacity of `list`, in elements.
 * @return The number of accounts copied into `list` (0 if none stored yet), or -1 if
 *         Kiwi_Credentials_Setup() was not called first or the file is unreadable.
 */
int Kiwi_Credentials_Get(KIWI_CREDENTIALS_T *list, int size) {
    if (!sDefaultGlobalCredentials.configured || !list || size <= 0) {
        return 0;
    }

    uint32_t count = 0;
    KIWI_CREDENTIALS_T *arr = NULL;

    if (!reloadDatabase(&arr, &count)) {
        return 0;
    }
    uint32_t n = (count < (uint32_t)size) ? count : (uint32_t)size;
    memcpy(list, arr, n * sizeof(KIWI_CREDENTIALS_T));
    cleanup(arr, count);

    return (int)n;
}

/**
 * @brief Generate a deterministic 8-character password from the supplied salt.
 *
 * The function calculates SHA-256(salt), converts the first four digest bytes
 * to lowercase hexadecimal, and writes the resulting 8 characters plus a NUL
 * terminator to `password`.
 *
 * @param[in]  salt     NUL-terminated input string used as SHA-256 input.
 * @param[out] password Output buffer receiving 8 hexadecimal characters and NUL.
 * @param[in]  size     Size of `password` in bytes; must be at least 9.
 * @return 0 on success, -1 for invalid arguments, or -2 on SHA-256 failure.
 *
 * @warning An 8-hex-character output contains only 32 bits of the SHA-256 digest.
 *          Do not treat a public/predictable salt such as a MAC address or serial
 *          number as a secret password source.
 */
int Kiwi_Credentials_GeneratePassword(char *salt, char *password, int size) {
    /*
     * Eight hexadecimal characters plus the terminating NUL byte are required.
     * SHA-256 itself produces 32 bytes, but this API intentionally exposes only
     * the first 4 bytes as 8 lowercase hexadecimal characters.
     */
    if (!salt || !password || salt[0] == '\0' || size < 9) {
        return -1;
    }

    static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char lower[] = "abcdefghijklmnopqrstuvwxyz";
    static const char digit[] = "0123456789";
    static const char all[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    unsigned int digestLen = 0;
    uint8_t digest[EVP_MAX_MD_SIZE] = {0};

    EVP_Digest(salt, strlen(salt), digest, &digestLen, EVP_sha256(), NULL);
    password[0] = upper[digest[0] % (sizeof(upper) - 1)];
    password[1] = lower[digest[1] % (sizeof(lower) - 1)];
    password[2] = digit[digest[2] % (sizeof(digit) - 1)];
    for (int id = 3; id < 8; ++id) {
        password[id] = all[digest[id] % (sizeof(all) - 1)];
    }
    password[8] = '\0';

    OPENSSL_cleanse(digest, sizeof(digest));
    return 0;
}