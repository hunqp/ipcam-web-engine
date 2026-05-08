#include "jwt.h"
#include "authorise.h"

bool AUTHORISE_JWT_ValidateToken(const std::string& token) {
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

std::string AUTHORISE_JWT_GenerateToken(const std::string& username) {
    auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_subject(username)
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(JWT_EXPIRED_SECONDS))
        .sign(jwt::algorithm::hs256(JWT_AUTHORISE_SECRET));
}