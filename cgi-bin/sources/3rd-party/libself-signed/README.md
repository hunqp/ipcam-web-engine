# self-sign

A small C library for verifying signed firmware upgrade authorization tokens.

## What it does

`self-sign` validates a token of the form:

```
Base64("<md5>|<expiry>").Base64(signature)
```

It decodes both parts, checks the SHA-256 signature against a PEM X.509
certificate (`/oem/usr/etc/lighttpd/server.crt`), confirms the signed MD5
matches the firmware's checksum, and rejects the token if it has expired.

## Why this exists

Firmware upgrades are risky if anyone can push an arbitrary file to a
device. Before a device installs `p2p_client.tar`, it should make sure the
upgrade was actually authorized by someone holding the private key that
matches `server.crt`. That's what `validateSecret()` checks: it doesn't sign
anything itself — a server elsewhere signs `"<md5>|<expiry>"` with its
private key, and this library only verifies that signature with the
matching public certificate.

## The token, piece by piece

A token looks like this:

```
ZjNhMWIyYzMuLi58MTc5MzE0MDAwMA==.MEUCIQD3k... (signature bytes, base64)
```

It's two Base64 blobs joined by a dot:

| Part        | Contents                            | Example (decoded)        |
|-------------|--------------------------------------|---------------------------|
| `payload`   | `"<md5>\|<expiry>"`                   | `a1b2c3...d4\|1793140000` |
| `signature` | Raw SHA-256 signature over `payload`, made with the server's private key | (binary bytes) |

- `md5` — the expected MD5 checksum of `p2p_client.tar` (32 hex chars)
- `expiry` — a Unix timestamp; the token is invalid once the current time
  passes it

## Validation flow (step by step)

```
   secret token, md5sum
          │
          ▼
 1. Trim whitespace, split token on "." into payload/signature
          │
          ▼
 2. Base64-decode both parts
          │
          ▼
 3. Verify signature over the decoded payload using server.crt (SHA-256)
          │   fails → reject: "Signature verification failed"
          ▼
 4. Split decoded payload on "|" into signedMd5 / expiryString
          │
          ▼
 5. Compare signedMd5 (from the token) with md5sum (of the actual file)
          │   mismatch → reject: "Firmware MD5 does not match signed MD5"
          ▼
 6. Parse expiryString, compare against the current time
          │   expired → reject: "Secret token has expired"
          ▼
 7. All checks passed → accept, print remaining validity time
```

Each step fails closed: if anything is malformed, mismatched, unparsable,
or expired, `validateSecret()` returns `false` and a short reason is
printed to `stderr`.

## Files

- `self-sign.h` — public API
- `self-sign.c` — implementation (OpenSSL or mbedTLS backend, selected via
  `USE_OPENSSL` / `USE_MBEDTLS`)

## Build

```sh
gcc -c -DUSE_OPENSSL self-sign.c -o self-sign.o
```

## API

```c
bool validateSecret(const char *secret, const char *md5sum);
```

Returns `true` only if the token's signature, MD5, and expiry all check out.

## Usage example

```c
#include "self-sign.h"

int main(void) {
    const char *token  = "ZjNhMWIyYzMuLi58MTc5MzE0MDAwMA==.MEUCIQD3k...";
    const char *md5sum = "a1b2c3d4e5f6...";  /* MD5 of p2p_client.tar */

    if (validateSecret(token, md5sum)) {
        /* proceed with the firmware upgrade */
    } else {
        /* reject the upgrade; reason was already printed to stderr */
    }

    return 0;
}
```

## Glossary (for newcomers)

- **Base64** — a way of encoding binary data as plain text so it's safe to
  put in URLs, headers, or a single-line token.
- **MD5** — a checksum/hash of a file, used here just to name *which*
  firmware file was authorized (not for security — MD5 is not
  collision-resistant, it's only an identity check).
- **SHA-256 signature** — proof that whoever holds the private key matching
  `server.crt` approved this exact payload. Even one changed byte in the
  payload makes the signature invalid.
- **Expiry (Unix timestamp)** — a number of seconds since Jan 1, 1970 UTC;
  the token becomes worthless after that moment, so a leaked token can't be
  replayed forever.