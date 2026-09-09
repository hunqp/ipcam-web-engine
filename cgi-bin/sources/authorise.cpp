#include <random>
#include "jwt.h"
#include "authorise.h"

static std::string JWT_AUTHORISE_SECRET;

void jwt_authorise_setup(void) {
    static const size_t length = 32;
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static std::random_device rd;
    static std::mt19937 generator(rd());
    std::uniform_int_distribution<size_t> distribution(0, chars.size() - 1);

    JWT_AUTHORISE_SECRET.reserve(32);
    for (size_t i = 0; i < length; ++i) {
        JWT_AUTHORISE_SECRET += chars[distribution(generator)];
    }

    JWT_AUTHORISE_SECRET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefg"; // test
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

std::string jwt_authorise_generate_token(const std::string& username, int role) {
    auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_subject(username)
        .set_payload_claim("role", jwt::claim(std::to_string(role)))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(JWT_AUTHORISE_EXPIRED_SECONDS))
        .sign(jwt::algorithm::hs256(JWT_AUTHORISE_SECRET));
}

int jwt_authorise_get_role(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        if (decoded.has_payload_claim("role")) {
            std::string role_str = decoded.get_payload_claim("role").as_string();
            return std::stoi(role_str);
        }
    } catch (...) {
        /**/
    }
    return 0xff;
}