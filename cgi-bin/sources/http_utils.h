#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

#include <string>
#include "fcgiapp.h"
#include "authorise.h"

/*
    @HTTP Reponser Functions
*/
extern void HTTP_ResponseRedirect(FCGX_Request& message, const std::string& place);
extern void HTTP_ResponseDataAsJSON(FCGX_Request& message, int code, const std::string& content, const std::string& extraHeader = "");
extern void HTTP_ResponseDataAsHTML(FCGX_Request& message, int code, const std::string& html);
extern void HTTP_ResponseDataAsBinaries(FCGX_Request& message, int code, const std::string& contentType, const std::string& body);
extern void HTTP_ResponseDataAsChunkBinaries(FCGX_Request& message, int code, const std::string& contentType, const std::string& body, long long start, long long end, long long total);
extern void HTTP_ResponseRangeNotSatisfiable(FCGX_Request& message, long long total);

/*
    @HTTP Helper Functions
*/
extern void HTTP_DecodeSubmitForm(char *src, char *dst);
extern bool HTTP_ExtractRangeHeader(const std::string& range, long long total, long long& start, long long& end);
extern size_t HTTP_ExtractBodyContentLength(FCGX_Request& message);
extern std::string HTTP_ExtractBodyContent(FCGX_Request& message);
extern std::string HTTP_GenerateCookies(const std::string& username, int role);
extern bool HTTP_IsAuthenticated(FCGX_Request& message, int *role = NULL);

#endif /* HTTP_UTILS_H */
