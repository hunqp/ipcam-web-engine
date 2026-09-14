#include <ctime>
#include <random>
#include <cstring>
#include <fstream>
#include <iterator>
#include <pthread.h>
#include <unordered_map>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>

#include "jwt.h"
#include "main.h"
#include "cgi_debug.h"
#include "authorise.h"

/* Mirrored logout denylist under tmpfs (RAM_ROOT), not flash: it must survive
 * a FastCGI process restart, but there is no reason to wear flash for state
 * that a full device reboot is allowed to drop (see authorise.h). */
#define APP_JWT_RECLAIMS_FILE   RAM_ROOT "/jwt-reclaims"

/* Set only once jwt_authorise_setup() has derived a real, device-unique
 * secret. While false every authentication entry point fails closed: no
 * token is ever issued or accepted on a fixed/predictable/absent secret. */
static bool sSecretValid = false;
static std::string JWT_AUTHORISE_SECRET;

/* Logout denylist: the "jti" of every token that has been logged out, mapped
 * to that token's own expiry so entries can be pruned once they would be
 * rejected anyway. Each token carries a unique random jti, so this revokes
 * exactly one session with no timing ambiguity. Size is bounded by (active
 * users x logouts within one token lifetime); tiny in practice and pruned on
 * every insert. Guarded by a mutex because the WebSocket auth hook runs on a
 * different thread from the FastCGI loop. Mirrored to APP_JWT_RECLAIMS_FILE
 * on every change and reloaded at setup so it survives a process restart;
 * lost only on a full device reboot (RAM_ROOT is tmpfs), after which a
 * logged-out token relies on its <= JWT_AUTHORISE_EXPIRED_SECONDS expiry. */
static pthread_mutex_t sMutex = PTHREAD_MUTEX_INITIALIZER;
static std::unordered_map<std::string, uint32_t> sReclaims;

/* Rewrites APP_JWT_RECLAIMS_FILE from the current (already-pruned) sReclaims.
 * Caller must hold sMutex. Best-effort: a failed write only reverts to the
 * pre-persistence behaviour (denylist lost on process restart), it never
 * blocks a logout. */
static void jwt_authorise_persist_reclaims_locked(void) {
    std::ofstream out(APP_JWT_RECLAIMS_FILE, std::ios::trunc);
    for (const auto& kv : sReclaims) {
        out << kv.first << ' ' << kv.second << '\n';
    }
}

/* Loads the denylist mirrored by a prior process, dropping anything that
 * would already be rejected on its own expiry. Called once from
 * jwt_authorise_setup(). */
static void jwt_authorise_load_reclaims(void) {
    std::ifstream in(APP_JWT_RECLAIMS_FILE);
    if (!in) {
        return;
    }
    uint32_t now = (uint32_t)time(NULL);
    std::string jti;
    uint32_t expiresAt;

    pthread_mutex_lock(&sMutex);
    while (in >> jti >> expiresAt) {
        if (expiresAt > now) {
            sReclaims[jti] = expiresAt;
        }
    }
    pthread_mutex_unlock(&sMutex);
}

/* Lowercase hex of the first `nbytes` of SHA-256(in). Returns "" on failure. */
static std::string SHA256_Hex(const std::string& in, int nbytes) {
    unsigned int mdlen = 0;
    unsigned char md[EVP_MAX_MD_SIZE];
    
    if (in.empty() ||
        EVP_Digest(in.data(), in.size(), md, &mdlen, EVP_sha256(), NULL) != 1) {
        return "";
    }
    if (nbytes < 1) 
        nbytes = 1;
    if (nbytes > (int)mdlen) 
        nbytes = (int)mdlen;

    static const char hex[] = "0123456789abcdef";
    std::string str;
    str.reserve(nbytes * 2);
    for (int i = 0; i < nbytes; ++i) {
        str += hex[md[i] >> 4];
        str += hex[md[i] & 0x0f];
    }
    return str;
}

/* Parses a PEM private key file and returns its raw DER bytes. Parsing
 * (instead of slurping the file's raw bytes) validates that the path really
 * points at a private key, so a missing/empty/garbage file is caught here
 * rather than silently hashed into a predictable "secret". Caller owns
 * *outDer and must OPENSSL_cleanse()+OPENSSL_free() it. Returns -1 on any
 * failure, with *outDer left NULL. */
static int jwt_extract_private_key_der(const char* filename, uint8_t** outDer, int* outLen) {
    *outDer = NULL;
    *outLen = 0;

    BIO* bio = BIO_new_file(filename, "r");
    if (!bio) {
        return -1;
    }
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) {
        return -1;
    }

    uint8_t* der = NULL;
    int len = i2d_PrivateKey(pkey, &der);
    EVP_PKEY_free(pkey);
    if (len <= 0 || !der) {
        return -1;
    }

    *outDer = der;
    *outLen = len;
    return 0;
}

/* HKDF-SHA256(salt, secret) -> PRK, then HKDF-Expand(PRK, info) -> 32-byte
 * key. Domain-separated from Kiwi_Credentials' own derivation off the same
 * key file (distinct salt/info), so the two derived keys are cryptographically
 * independent despite sharing a root secret. */
static void jwt_derive_secret_key(const uint8_t* secret, int secretLen, uint8_t key[32]) {
    unsigned int prkLen = 0;
    uint8_t prk[EVP_MAX_MD_SIZE] = {0};
    static const uint8_t SALT[] = "IPCAM_JWT_HS256_V1_SALT";
    HMAC(EVP_sha256(), SALT, sizeof(SALT) - 1, secret, secretLen, prk, &prkLen);

    static const uint8_t INFO[] = "IPCAM_JWT_HS256_V1_SECRET";
    uint8_t info[sizeof(INFO)];
    memcpy(info, INFO, sizeof(INFO) - 1);
    info[sizeof(INFO) - 1] = 0x01; /* HKDF-Expand block counter */

    unsigned int okmLen = 0;
    uint8_t okm[EVP_MAX_MD_SIZE] = {0};
    HMAC(EVP_sha256(), prk, prkLen, info, sizeof(info), okm, &okmLen);
    memcpy(key, okm, 32);

    OPENSSL_cleanse(prk, sizeof(prk));
    OPENSSL_cleanse(okm, sizeof(okm));
}

bool jwt_authorise_setup(void) {
    /* Derive the HS256 signing secret from the device-unique TLS private key
     * (APP_SECRET_UNIQUE_FILE, the same file Kiwi_Credentials feeds into
     * HKDF, via its own domain-separated derivation). Advantages over a
     * per-process random secret:
     *   - stable across restarts, so a FastCGI crash no longer silently
     *     invalidates every live session;
     *   - device-specific, so a token minted on one unit is useless on another;
     *   - never embedded in the source tree or the binary. */
    JWT_AUTHORISE_SECRET.clear();
    sSecretValid = false;

    uint8_t* der = NULL;
    int derLen = 0;
    if (jwt_extract_private_key_der(APP_SECRET_UNIQUE_FILE, &der, &derLen) != 0) {
        CGI_SYSE("jwt_authorise_setup: cannot read/parse device private key '%s'; "
                 "refusing to issue or accept session tokens\r\n", APP_SECRET_UNIQUE_FILE);
        return false;
    }

    uint8_t key[32];
    jwt_derive_secret_key(der, derLen, key);
    OPENSSL_cleanse(der, (size_t)derLen);
    OPENSSL_free(der);

    static const char hex[] = "0123456789abcdef";
    JWT_AUTHORISE_SECRET.reserve(sizeof(key) * 2);
    for (size_t i = 0; i < sizeof(key); ++i) {
        JWT_AUTHORISE_SECRET += hex[key[i] >> 4];
        JWT_AUTHORISE_SECRET += hex[key[i] & 0x0f];
    }
    OPENSSL_cleanse(key, sizeof(key));

    sSecretValid = true;
    jwt_authorise_load_reclaims();
    return true;
}

bool jwt_authorise_validate_token(const std::string& token) {
    if (!sSecretValid) {
        return false;   /* no device-derived secret: never trust a signature */
    }
    try {
        auto decoded = jwt::decode(token);
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256(JWT_AUTHORISE_SECRET))
            .verify(decoded);
        return true;
    }
    catch (...) {
        /**/
    }
    return false;
}

/* Fingerprint of an account's current secret material. It is baked into the
 * token at login (claim "cred") and re-checked on every request, so changing
 * the password or deleting the account invalidates tokens already issued. */
static inline std::string jwt_get_credential_tag(const KIWI_CREDENTIALS_T& acc) {
    return SHA256_Hex(std::string("cred:") + acc.username + "\x1f" + acc.password, 8);
}

std::string jwt_authorise_get_credentials(const std::string& username) {
    KIWI_CREDENTIALS_T list[32] = {0};
    int n = Kiwi_Credentials_Get(list, 32);
    for (int i = 0; i < n; ++i) {
        if (username == list[i].username) {
            return jwt_get_credential_tag(list[i]);
        }
    }
    return "";
}

/* 128-bit random token id (32 hex chars), unique per issued token. */
static std::string jwt_new_token_id(void) {
    std::random_device rd;
    static const char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (int i = 0; i < 16; ++i) {
        unsigned b = rd() & 0xFFu;
        id += hex[b >> 4];
        id += hex[b & 0x0F];
    }
    return id;
}

void jwt_authorise_reclaim_token(const std::string& token) {
    if (token.empty() || !jwt_authorise_validate_token(token)) {
        return;
    }
    std::string jti;
    uint32_t expiresAt = 0;
    try {
        auto decoded = jwt::decode(token);
        if (decoded.has_id()) {
            jti = decoded.get_id();
        }
        if (decoded.has_expires_at()) {
            expiresAt = (uint32_t)std::chrono::system_clock::to_time_t(decoded.get_expires_at());
        }
    }
    catch (...) {
        return;
    }
    if (jti.empty()) {
        return;
    }

    uint32_t now = (uint32_t)time(NULL);
    if (expiresAt <= now) {
        return;   /* already dead on its own */
    }

    pthread_mutex_lock(&sMutex);

    for (std::unordered_map<std::string, uint32_t>::iterator it = sReclaims.begin(); it != sReclaims.end();) {
        it = (it->second <= now) ? sReclaims.erase(it) : std::next(it);
    }
    sReclaims[jti] = expiresAt;
    jwt_authorise_persist_reclaims_locked();

    pthread_mutex_unlock(&sMutex);

    CGI_SYSD("jwt_authorise_reclaim_token: revoked jti=%s (expires in %u s)\r\n",
             jti.c_str(), (unsigned)(expiresAt - now));
}

static bool jwt_token_id_is_revoked(const std::string& jti) {
    if (jti.empty()) {
        return true;
    }
    
    pthread_mutex_lock(&sMutex);

    auto ret = sReclaims.find(jti) != sReclaims.end();

    pthread_mutex_unlock(&sMutex);

    return ret;
}

std::string jwt_authorise_generate_token(const std::string& username, int role,
                                         const std::string& credTag) {
    if (!sSecretValid) {
        CGI_SYSE("jwt_authorise_generate_token: no valid signing secret; refusing to issue token for '%s'\r\n",
                 username.c_str());
        return "";
    }
    auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_id(jwt_new_token_id())
        .set_subject(username)
        .set_payload_claim("role", jwt::claim(std::to_string(role)))
        .set_payload_claim("cred", jwt::claim(credTag))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(JWT_AUTHORISE_EXPIRED_SECONDS))
        .sign(jwt::algorithm::hs256(JWT_AUTHORISE_SECRET));
}

bool jwt_authorise_check(const std::string& token, int* role) {
    if (token.empty() || !jwt_authorise_validate_token(token)) {
        return false;
    }

    std::string sub, cred, jti;
    try {
        auto decoded = jwt::decode(token);
        if (decoded.has_subject()) {
            sub = decoded.get_subject();
        }
        if (decoded.has_payload_claim("cred")) {
            cred = decoded.get_payload_claim("cred").as_string();
        }
        if (decoded.has_id()) {
            jti = decoded.get_id();
        }
    }
    catch (...) {
        return false;
    }
    if (sub.empty()) {
        return false;
    }
    if (jwt_token_id_is_revoked(jti)) {
        CGI_SYSW("jwt_authorise_check: rejected revoked/expired token (sub=%s, jti=%s)\r\n",
                 sub.c_str(), jti.c_str());
        return false;   /* logged out, or jti unknown/empty */
    }

    /* Re-bind to the live account: the accounts DB - not the token - is the
     * source of truth for "does this user exist" and "what is their role". */
    KIWI_CREDENTIALS_T list[32] = {0};
    int n = Kiwi_Credentials_Get(list, 32);
    for (int i = 0; i < n; ++i) {
        if (sub != list[i].username) {
            continue;
        }
        if (cred != jwt_get_credential_tag(list[i])) {
            return false;
        }
        if (role) {
            *role = list[i].role;
        }
        return true;
    }
    return false;
}
