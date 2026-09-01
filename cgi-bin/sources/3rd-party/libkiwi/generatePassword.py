import sys
import hashlib

def generatePassword(salt):
    upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    lower = "abcdefghijklmnopqrstuvwxyz"
    digit = "0123456789"
    chars = upper + lower + digit

    digest = hashlib.sha256(salt.encode()).digest()

    password  = upper[digest[0] % len(upper)]
    password += lower[digest[1] % len(lower)]
    password += digit[digest[2] % len(digit)]

    for i in range(3, 8):
        password += chars[digest[i] % len(chars)]

    return password


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <salt>")
        sys.exit(1)

    salt = sys.argv[1]
    password = generatePassword(salt)

    print(f"Salt     : {salt}")
    print(f"Password : {password}")