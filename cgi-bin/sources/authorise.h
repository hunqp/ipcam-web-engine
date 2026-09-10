#ifndef AUTHORISE_H
#define AUTHORISE_H

#include <string>

#define JWT_AUTHORISE_SESSION               (char*)"vivoo-session"
/* Short lifetime. Logout revokes the token immediately via an in-RAM denylist
 * (jwt_authorise_reclaim_token); a credential/role change is caught on the next
 * request by the account re-bind in jwt_authorise_check(). This bound only caps
 * a token whose revocation was lost to a process restart, or one leaked and
 * replayed before its owner logs out. */
#define JWT_AUTHORISE_EXPIRED_SECONDS       (3600)

/*
    @JWT Token Generator
*/
extern void jwt_authorise_setup(void);
extern bool jwt_authorise_validate_token(const std::string& token);
extern std::string jwt_authorise_get_credentials(const std::string& username);
extern std::string jwt_authorise_generate_token(const std::string& username, int role, const std::string& credTag);

/* Verify signature + expiry, then re-bind the token to the live account:
 * succeeds only if the account still exists, its password is unchanged since
 * the token was issued, and the token has not been logged out. On success
 * *role is the CURRENT role read from the accounts DB, never the "role" claim. */
extern bool jwt_authorise_check(const std::string& token, int* role);

/* Add a token to the in-RAM (never persisted) logout denylist so every later
 * jwt_authorise_check() of it fails. Safe to call with "" or an invalid token
 * (no-op). The entry is dropped automatically once the token would expire; the
 * whole denylist is lost on process restart, after which a logged-out token
 * relies on its <= JWT_AUTHORISE_EXPIRED_SECONDS expiry. */
extern void jwt_authorise_reclaim_token(const std::string& token);

#endif /* AUTHORISE_H */