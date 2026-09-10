#include <ctime>
#include <random>
#include <fstream>
#include <iterator>
#include <pthread.h>
#include <unordered_map>
#include <openssl/evp.h>

#include "jwt.h"
#include "main.h"
#include "authorise.h"

static std::string JWT_AUTHORISE_SECRET;

/* In-RAM logout denylist, never written to disk: the "jti" of every token that
 * has been logged out, mapped to that token's own expiry so entries can be
 * pruned once they would be rejected anyway. Each token carries a unique random
 * jti, so this revokes exactly one session with no timing ambiguity. Size is
 * bounded by (active users x logouts within one token lifetime); tiny in
 * practice and pruned on every insert. Guarded by a mutex because the WebSocket
 * auth hook runs on a different thread from the FastCGI loop. Lost on process
 * restart, after which a logged-out token relies on its <= 15 min expiry. */
static pthread_mutex_t sMutex = PTHREAD_MUTEX_INITIALIZER;
static std::unordered_map<std::string, uint32_t> sReclaims;

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

void jwt_authorise_setup(void) {
    /* Derive the HS256 signing secret from the device-unique TLS private key
     * (APP_SECRET_UNIQUE_FILE, the same file Kiwi_Credentials feeds into HKDF).
     * Advantages over the previous per-process random secret:
     *   - stable across restarts, so a FastCGI crash no longer silently
     *     invalidates every live session;
     *   - device-specific, so a token minted on one unit is useless on another;
     *   - never embedded in the source tree or the binary. */
    std::ifstream f(APP_SECRET_UNIQUE_FILE, std::ios::binary);
    std::string material((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());

    JWT_AUTHORISE_SECRET = SHA256_Hex("jwt-hs256:" + material, 32);
    if (!JWT_AUTHORISE_SECRET.empty()) {
        return;
    }

    /* Fallback (unprovisioned device / key unreadable): fail closed with a
     * random per-process secret rather than a predictable constant. */
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<size_t> distribution(0, chars.size() - 1);
    JWT_AUTHORISE_SECRET.clear();
    for (size_t i = 0; i < 64; ++i) {
        JWT_AUTHORISE_SECRET += chars[distribution(generator)];
    }
}

bool jwt_authorise_validate_token(const std::string& token) {
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

    pthread_mutex_unlock(&sMutex);
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
        return false;   /* logged out */
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
