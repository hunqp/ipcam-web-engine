#!/bin/sh
set -eu

# Usage:
#   sh generate-cert.sh CAMERA_SERIAL
#
# Example:
#   sh generate-cert.sh VVCAM2026000123

DEVICE_SERIAL="${1:-${DEVICE_SERIAL:-}}"

if [ -z "$DEVICE_SERIAL" ]; then
    echo "Usage: $0 CAMERA_SERIAL" >&2
    exit 1
fi

# Restrict the serial number to characters safe for:
# - X.509 subject fields
# - DNS hostnames
case "$DEVICE_SERIAL" in
    *[!A-Za-z0-9._-]*)
        echo "Error: camera serial may contain only A-Z, a-z, 0-9, '.', '_' and '-'" >&2
        exit 1
        ;;
esac

SERVER_KEY="server.key"
SERVER_CRT="server.crt"

# Convert to lowercase for DNS usage.
DNS_SERIAL=$(printf '%s' "$DEVICE_SERIAL" | tr '[:upper:]_' '[:lower:]-')
HOSTNAME="vivooeye-camera-${DNS_SERIAL}"

# A DNS label must not exceed 63 characters.
if [ "${#HOSTNAME}" -gt 63 ]; then
    echo "Error: generated hostname is longer than 63 characters" >&2
    exit 1
fi

# Generate a deterministic X.509 serial number from the camera serial.
#
# RFC 5280 limits certificate serial numbers to 20 octets.
# Prefixing 01 and using 38 hash characters produces exactly 20 octets
# and guarantees a positive, non-zero serial number.
SERIAL_HASH=$(
    printf '%s' "$DEVICE_SERIAL" |
        openssl dgst -sha256 |
        sed 's/^.*= //' |
        cut -c1-38
)

CERT_SERIAL="0x01${SERIAL_HASH}"

echo "Generating certificate for camera: $DEVICE_SERIAL"
echo "Certificate hostname: $HOSTNAME"
echo "X.509 serial number: $CERT_SERIAL"

# Generate a unique private key.
openssl ecparam \
    -genkey \
    -name prime256v1 \
    -noout \
    -out "$SERVER_KEY"

# Generate a self-signed server certificate.
openssl req \
    -new \
    -x509 \
    -key "$SERVER_KEY" \
    -out "$SERVER_CRT" \
    -days 3650 \
    -sha256 \
    -set_serial "$CERT_SERIAL" \
    -subj "/C=VN/ST=Ho Chi Minh/L=Ho Chi Minh/O=VIVOO TECHNOLOGY CO., LTD/OU=R&D/serialNumber=${DEVICE_SERIAL}/CN=${HOSTNAME}" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature,keyAgreement" \
    -addext "extendedKeyUsage=serverAuth" \
    -addext "subjectAltName=DNS:${HOSTNAME},DNS:vivooeye-camera,URI:urn:vivooeye:camera:${DEVICE_SERIAL}"

chmod 600 "$SERVER_KEY"
chmod 644 "$SERVER_CRT"

echo "Generated:"
echo "  Private key: $SERVER_KEY"
echo "  Certificate: $SERVER_CRT"