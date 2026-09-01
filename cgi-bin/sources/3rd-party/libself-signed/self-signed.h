/*****************************************************************************
 **             __      _________      ______   ____                        **
 **             \ \    / /_   _\ \    / / __ \ / __ \                       **
 **              \ \  / /  | |  \ \  / / |  | | |  | |                      **
 **               \ \/ /   | |   \ \/ /| |  | | |  | |                      **
 **                \  /   _| |_   \  / | |__| | |__| |                      **
 **                 \/   |_____|   \/   \____/ \____/                       **
 ** ------------------------------------------------------------------------**
 **                                                                         **
 ** HungPNQ                                                                 **
 **                                                                         **
 ** VIVOO COMPANY LTD.                                                      **
 **                                                                         **
 ** 26/07/2026                                                              **
 **                                                                         **
 ** SELF-SIGNED TOKEN VERIFICATION                                          **
 **                                                                         **
 ** Description: Verification of self-signed firmware upgrade authorization **
 ** tokens                                                                  **
 **                                                                         **
 *****************************************************************************
 */

#ifndef SELF_SIGN_H
#define SELF_SIGN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

/**
 * Verify a SHA-256 digital signature using a PEM X.509 certificate.
 *
 * @param payload       Exact decoded payload bytes: "<md5>|<expiry>".
 * @param payloadLen    Length of payload in bytes.
 * @param signature     Raw decoded signature bytes.
 * @param signatureLen  Number of signature bytes.
 * @param certFile      Path to the PEM certificate.
 *
 * @return true when signature verification succeeds.
 */
extern bool verifySignature(
    const char *payload, size_t payloadLen,
    const unsigned char *signature, size_t signatureLen,
    const char *certFile);

/**
 * Validate an upgrade authorization token.
 *
 * Token format:
 *
 *     Base64("<md5>|<expiry>").Base64(signature)
 *
 * Validation:
 *   1. Split the token into payload and signature.
 *   2. Base64-decode both components.
 *   3. Verify the signature with server.crt.
 *   4. Compare the signed MD5 with md5sum.
 *   5. Check that the expiry timestamp has not passed.
 *
 * @param secret  NUL-terminated signed token received from the user.
 * @param md5sum  NUL-terminated MD5 checksum calculated from p2p_client.tar.
 *
 * @return true only when every validation step succeeds.
 */
extern bool validateSecret(const char *secret, char *md5sum, char **signature, char **errorStr);

#ifdef __cplusplus
}
#endif

#endif /* SELF_SIGN_H */