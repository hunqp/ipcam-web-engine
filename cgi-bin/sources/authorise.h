#ifndef AUTHORISE_H
#define AUTHORISE_H

#include <string>

#define JWT_AUTHORISE_SESSION               (char*)"vivoo_session"
/* This create by command "$(openssl rand -hex 32)" */
#define JWT_AUTHORISE_SECRET                (char*)"f22b57e4b65f73598895a6fc4255b0311c6878793d9d21ed0594a7214cc5380a"
#define JWT_AUTHORISE_EXPIRED_SECONDS       (3600)

/*
    @JWT Token Generator
*/
extern int jwt_authorise_get_role(const std::string& token);
extern bool jwt_authorise_validate_token(const std::string& token);
extern std::string jwt_authorise_generate_token(const std::string& username, int role = 2);

#endif /* AUTHORISE_H */