#ifndef KIWI_DATABASE_H
#define KIWI_DATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

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
extern void Kiwi_Database_Setup(const char *storageKeyFilename);

/**
 * @brief Encrypt `plaintext` with AES-256-GCM and write it to `filename`, replacing
 *        any existing content. Intended for small, individually-keyed blobs such as
 *        an access token, e.g. one file per item rather than a multi-record database.
 * @param[in] filename The path of the file to write the encrypted blob into.
 * @param[in] plaintext The data to encrypt and store.
 * @param[in] len Length of `plaintext`, in bytes. Nothing is written if Setup() was
 *            not called first, or if `filename`/`plaintext` is NULL or `len` is 0.
 */
extern void Kiwi_Database_Save(const char *filename, uint8_t *plaintext, uint32_t len);

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
extern void Kiwi_Database_Read(const char *filename, uint8_t *buffer, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* KIWI_DATABASE_H */
