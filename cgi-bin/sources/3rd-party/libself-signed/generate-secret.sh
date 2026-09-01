###############################################################################
# Firmware Upgrade Secret Token Generator
#
# Description:
#   This script generates a signed secret token for a firmware package.
#   The secret token is required by the device before a firmware upgrade is
#   accepted.
#
# Flow:
#   1. Calculate the MD5 checksum of the firmware package (p2p_client.tar).
#   2. Generate an expiration timestamp (current time + 1 hour).
#   3. Create a payload in the following format:
#
#        <package_md5>|<expiration_timestamp>
#
#   4. Digitally sign the payload using the RSA private key (server.key).
#   5. Base64 encode both the payload and its signature.
#   6. Generate the final secret token:
#
#        Base64(payload).Base64(signature)
#
# Device Validation:
#   1. Split the token into payload and signature.
#   2. Decode both Base64 values.
#   3. Verify the signature using the corresponding public key (server.crt or
#      extracted public key).
#   4. Parse the payload to obtain the package checksum and expiration time.
#   5. Calculate the MD5 of the received firmware package.
#   6. Compare the calculated MD5 with the payload MD5.
#   7. Verify that the current time has not exceeded the expiration timestamp.
#   8. If all checks pass, the firmware upgrade is authorized.
#
# Note:
#   - The generated token is valid for one hour.
#   - The signature prevents modification of the checksum or expiration time.
#   - SHA-256 is recommended instead of MD5 for new implementations.
###############################################################################

#!/bin/sh

set -eu

SECRET_FILE="secret.txt"
PAYLOAD_FILE="payload.txt"
SIGNATURE_FILE="signature.bin"
PACKAGE_FILE="p2p_client.tar"
PRIVATE_KEY="$PWD/../oem/self-signed/server.key"

[ -f "$PACKAGE_FILE" ] || {
    echo "Package not found: $PACKAGE_FILE" >&2
    exit 1
}

[ -f "$PRIVATE_KEY" ] || {
    echo "Private key not found: $PRIVATE_KEY" >&2
    exit 1
}

PACKAGE_MD5=$(md5sum "$PACKAGE_FILE" | awk '{print $1}')
EXPIRATION=$(( $(date +%s) + 3600 ))

# Payload to sign: md5|expiration
printf '%s|%s' "$PACKAGE_MD5" "$EXPIRATION" > "$PAYLOAD_FILE"

openssl dgst -sha256 -sign "$PRIVATE_KEY" -out "$SIGNATURE_FILE" "$PAYLOAD_FILE"

PAYLOAD=$(base64 < "$PAYLOAD_FILE" | tr -d '\r\n')
SIGNATURE=$(base64 < "$SIGNATURE_FILE" | tr -d '\r\n')

# Final token: base64(payload).base64(signature)
printf '%s.%s\n' "$PAYLOAD" "$SIGNATURE" > "$SECRET_FILE"
rm -f "$PAYLOAD_FILE" "$SIGNATURE_FILE"

echo "MD5Sum: $PACKAGE_MD5"
echo "Expire: $EXPIRATION"
echo "Secret: $SECRET_FILE"