#ifndef KIWI_CREDENTIALS_H
#define KIWI_CREDENTIALS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    int role;
    char username[32];
    char password[32];
    uint32_t createTs;
    uint32_t expireTs;
} KIWI_CREDENTIALS_T;

/**
 * @brief Bind Add/Mod/Del/Get to one credentials file and derive its AES-256-GCM
 *        storage key from a device private key. Must be called before any other
 *        Kiwi_Credentials_*() call; calling it again rebinds to a new file/key.
 * @param[in] filename The path to the encrypted credentials file.
 * @param[in] privateKeyFile Path to a PEM-encoded private key file (e.g. the device's
 *            TLS server key). The key material is extracted from this file and fed
 *            into HKDF to derive the storage key; only the derived key is kept in
 *            memory, the key material itself is never stored.
 * @return 0 on success, or -1 if filename/privateKeyFile is NULL, or the private key
 *         file could not be read or parsed.
 */
extern int Kiwi_Credentials_Setup(const char *filename, const char *storageKeyFilename);

/**
 * @brief Add a new account, or replace the existing account with the same username.
 * @param[in] credentials The account to store.
 * @return 0 on success, or -1 if Kiwi_Credentials_Setup() was not called first, or the
 *         file could not be read/written.
 */
extern int Kiwi_Credentials_Add(KIWI_CREDENTIALS_T *credentials);

/**
 * @brief Update the existing account matched by credentials->username.
 * @param[in] credentials The new field values, keyed by username.
 * @return 0 on success, or -1 if Kiwi_Credentials_Setup() was not called first, no
 *         matching account was found, or the write failed.
 */
extern int Kiwi_Credentials_Mod(KIWI_CREDENTIALS_T *credentials);

/**
 * @brief Remove the account matched by credentials->username.
 * @param[in] credentials Only the username field is used to find the account.
 * @return 0 on success, or -1 if Kiwi_Credentials_Setup() was not called first, no
 *         matching account was found, or the write failed.
 */
extern int Kiwi_Credentials_Del(KIWI_CREDENTIALS_T *credentials);

/**
 * @brief Read back up to `size` stored accounts.
 * @param[out] list Caller-provided array of at least `size` elements.
 * @param[in] size The capacity of `list`, in elements.
 * @return The number of accounts copied into `list` (0 if none stored yet), or -1 if
 *         Kiwi_Credentials_Setup() was not called first or the file is unreadable.
 */
extern int Kiwi_Credentials_Get(KIWI_CREDENTIALS_T *list, int size);

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
extern int Kiwi_Credentials_GeneratePassword(char *salt, char *password, int size);

#ifdef __cplusplus
}
#endif

#endif /* KIWI_CREDENTIALS_H */
