#ifndef ATTEMPS_LOGIN_STATE_HH
#define ATTEMPS_LOGIN_STATE_HH

#include <cstdint>
#include <sys/socket.h>

/**
 * Per-client-IP login attempt / lockout tracking, used to slow down
 * brute-force password guessing WITHOUT letting a single attacker lock out
 * every other client (a global lockout is itself a trivial DoS: 15 bad
 * requests from anyone would block the legitimate owner too). Each source
 * IP gets its own counter and lockout state, isolating the blast radius of
 * an attack to the attacker's own address.
 *
 * The table backing this (see AttempsLogin.cpp) is a fixed-size, statically
 * allocated array with LRU eviction (no heap allocation, no unbounded
 * growth) so that an attacker connecting from many different source
 * addresses can, at worst, evict older entries - never exhaust memory or
 * crash the process.
 */
struct AttemptsLoginState {
    static const uint16_t U16_MAX_LOGIN_ATTEMPTS  = 5U;
    static const uint32_t U32_MAX_LOCKOUT_SECONDS = 300U; /* 5 minutes */
    /* Cap on the backoff exponent so the shift in onFailedAttempts() never
     * overflows and the lockout duration saturates at U32_MAX_LOCKOUT_SECONDS
     * well before that. */
    static const uint16_t U16_MAX_GEOMETRIC_STEPS = 4U;

    bool inUse = false;
    bool isLocked = false;
    uint32_t u32LockedTime = 0;
    uint32_t u32LastAccess = 0;
    uint16_t u16FailedAttempts = 0;
    uint16_t u16GeometricSequence = 0;
    struct sockaddr_storage clientAddr = {};

    void reset(bool reasonRstByTimeout);

    /* Called on every failed login attempt from this IP. Locks it once
     * U16_MAX_LOGIN_ATTEMPTS is reached, with the lockout duration growing
     * geometrically (30s, 60s, 120s, ...) on each successive lockout, capped
     * at U32_MAX_LOCKOUT_SECONDS. */
    void onFailedAttempts(uint32_t u32Ts);

    /* Returns True if this IP should currently be blocked. Also transparently
     * unlocks it once the lockout period has expired. */
    bool isBlocked(uint32_t u32Ts);
};

/* Finds this client IP's tracking slot in the bounded table, creating one
 * (reusing the least-recently-used slot if the table is full) if it doesn't
 * exist yet. */
extern AttemptsLoginState* findOrCreateAttemptState(struct sockaddr_storage const& clientAddr, uint32_t u32Ts);

#endif /* ATTEMPS_LOGIN_STATE_HH */
