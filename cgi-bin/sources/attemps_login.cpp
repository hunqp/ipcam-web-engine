#include <cstring>
#include <netinet/in.h>

#include "attemps_login.h"

#define MAX_TRACKED_CLIENT_IPS 16U

static AttemptsLoginState sAttemptsList[MAX_TRACKED_CLIENT_IPS];

void AttemptsLoginState::reset(bool reasonRstByTimeout) {
    isLocked = false;
    u32LockedTime = 0;
    u16FailedAttempts = 0;
    if (!reasonRstByTimeout) {
        u16GeometricSequence = 0;
    }
}

void AttemptsLoginState::onFailedAttempts(uint32_t u32Ts) {
    u32LastAccess = u32Ts;
    if (++u16FailedAttempts < U16_MAX_LOGIN_ATTEMPTS) {
        return;
    }
    if (u16GeometricSequence < U16_MAX_GEOMETRIC_STEPS) {
        ++u16GeometricSequence;
    }
    isLocked = true;
    u16FailedAttempts = 0;
    u32LockedTime = u32Ts + (u16GeometricSequence * U32_MAX_LOCKOUT_SECONDS);
}

bool AttemptsLoginState::isBlocked(uint32_t u32Ts) {
    if (!isLocked) {
        return false;
    }
    if (u32Ts > u32LockedTime) {
        reset(true); /* Expired: unlock, but keep the geometric sequence */
        return false;
    }
    u32LastAccess = u32Ts;
    return true;
}

static bool sameClientAddress(struct sockaddr_storage const& a, struct sockaddr_storage const& b) {
    if (a.ss_family != b.ss_family) {
        return false;
    }
    if (a.ss_family == AF_INET) {
        return memcmp(&((struct sockaddr_in const&)a).sin_addr,
                      &((struct sockaddr_in const&)b).sin_addr,
                      sizeof(in_addr)) == 0;
    }
    if (a.ss_family == AF_INET6) {
        return memcmp(&((struct sockaddr_in6 const&)a).sin6_addr,
                      &((struct sockaddr_in6 const&)b).sin6_addr,
                      sizeof(in6_addr)) == 0;
    }
    return false;
}

AttemptsLoginState* findOrCreateAttemptState(struct sockaddr_storage const& clientAddr, uint32_t u32Ts) {
    AttemptsLoginState* pBusySlot = NULL;
    AttemptsLoginState* pFreeSlot = NULL;
    
    for (unsigned id = 0; id < MAX_TRACKED_CLIENT_IPS; ++id) {
        AttemptsLoginState* pSlot = &sAttemptsList[id];
        if (pSlot->inUse && sameClientAddress(pSlot->clientAddr, clientAddr)) {
            return pSlot;
        }
        if (!pSlot->inUse && pFreeSlot == NULL) {
            pFreeSlot = pSlot;
        }
        if (pBusySlot == NULL || pSlot->u32LastAccess < pBusySlot->u32LastAccess) {
            pBusySlot = pSlot;
        }
    }

    AttemptsLoginState* selected = (pFreeSlot != NULL) ? pFreeSlot : pBusySlot;
    *selected = AttemptsLoginState();
    selected->inUse = true;
    selected->clientAddr = clientAddr;
    selected->u32LastAccess = u32Ts;
    return selected;
}
