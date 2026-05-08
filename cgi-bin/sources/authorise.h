#ifndef AUTHORISE_H
#define AUTHORISE_H

#include <string>

#define JWT_AUTHORISE_SESSION   (char*)"vivoo_session"
/* This create by command "$(openssl rand -hex 32)" */
#define JWT_AUTHORISE_SECRET    (char*)"f22b57e4b65f73598895a6fc4255b0311c6878793d9d21ed0594a7214cc5380a"
#define JWT_EXPIRED_SECONDS     (180)

extern bool AUTHORISE_JWT_ValidateToken(const std::string& token);
extern std::string AUTHORISE_JWT_GenerateToken(const std::string& username);

#endif /* AUTHORISE_H */