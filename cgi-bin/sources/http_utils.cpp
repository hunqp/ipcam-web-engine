#include "json.hpp"
#include "http_utils.h"

extern std::string stGetEnvirVariables(FCGX_Request& request, const char* name);

static const char *HTTP_StatusReturn(int code) {
    switch (code) {
    /* 1xx Informational */
    case 100: return (const char*)"Continue";
    case 101: return (const char*)"Switching Protocols";
    case 102: return (const char*)"Processing";

    /* 2xx Success */
    case 200: return (const char*)"OK";
    case 201: return (const char*)"Created";
    case 202: return (const char*)"Accepted";
    case 203: return (const char*)"Non-Authoritative Information";
    case 204: return (const char*)"No Content";
    case 205: return (const char*)"Reset Content";
    case 206: return (const char*)"Partial Content";

    /* 3xx Redirection */
    case 300: return (const char*)"Multiple Choices";
    case 301: return (const char*)"Moved Permanently";
    case 302: return (const char*)"Found";
    case 303: return (const char*)"See Other";
    case 304: return (const char*)"Not Modified";
    case 307: return (const char*)"Temporary Redirect";
    case 308: return (const char*)"Permanent Redirect";

    /* 4xx Client Errors */
    case 400: return (const char*)"Bad Request";
    case 401: return (const char*)"Unauthorized";
    case 402: return (const char*)"Payment Required";
    case 403: return (const char*)"Forbidden";
    case 404: return (const char*)"Not Found";
    case 405: return (const char*)"Method Not Allowed";
    case 406: return (const char*)"Not Acceptable";
    case 407: return (const char*)"Proxy Authentication Required";
    case 408: return (const char*)"Request Timeout";
    case 409: return (const char*)"Conflict";
    case 410: return (const char*)"Gone";
    case 411: return (const char*)"Length Required";
    case 412: return (const char*)"Precondition Failed";
    case 413: return (const char*)"Payload Too Large";
    case 414: return (const char*)"URI Too Long";
    case 415: return (const char*)"Unsupported Media Type";
    case 416: return (const char*)"Range Not Satisfiable";
    case 417: return (const char*)"Expectation Failed";
    case 418: return (const char*)"I'm a teapot";
    case 429: return (const char*)"Too Many Requests";

    /* 5xx Server Errors */
    case 500: return (const char*)"Internal Server Error";
    case 501: return (const char*)"Not Implemented";
    case 502: return (const char*)"Bad Gateway";
    case 503: return (const char*)"Service Unavailable";
    case 504: return (const char*)"Gateway Timeout";
    case 505: return (const char*)"HTTP Version Not Supported";
    default:  return (const char*)"Unknown Status";
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void HTTP_ResponseRedirect(FCGX_Request& message, const std::string& place) {
    FCGX_FPrintF(message.out, "Status: 302 Found\r\n");
    FCGX_FPrintF(message.out, "Location: %s\r\n", place.c_str());
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(message.out, "Content-Length: 0\r\n");
    FCGX_FPrintF(message.out, "\r\n");
}

void HTTP_ResponseDataAsJSON(
    FCGX_Request& message, 
    int code, 
    const std::string& content,
    const std::string& extraHeader) {
    /**/
    FCGX_FPrintF(message.out, "Status: %d %s\r\n", code, HTTP_StatusReturn(code));
    FCGX_FPrintF(message.out, "Content-Type: application/json\r\n");
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    if (!extraHeader.empty()) {
        FCGX_FPrintF(message.out, "%s\r\n", extraHeader.c_str());
    }
    FCGX_FPrintF(message.out, "Content-Length: %d\r\n", (int)content.size());
    FCGX_FPrintF(message.out, "\r\n");
    FCGX_FPrintF(message.out, "%s", content.c_str());
}

void HTTP_ResponseDataAsHTML(
    FCGX_Request& message, 
    int code, 
    const std::string& html) {
    /**/
    FCGX_FPrintF(message.out, "Status: %d %s\r\n", code, HTTP_StatusReturn(code));
    FCGX_FPrintF(message.out, "Content-Type: text/html; charset=utf-8\r\n");
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(message.out, "Content-Length: %d\r\n", (int)html.size());
    FCGX_FPrintF(message.out, "\r\n");
    FCGX_FPrintF(message.out, "%s", html.c_str());
}

void HTTP_ResponseDataAsBinaries(
    FCGX_Request& message,
    int code,
    const std::string& contentType,
    const std::string& body) {
    /**/
    FCGX_FPrintF(message.out, "Status: %d %s\r\n", code, HTTP_StatusReturn(code));
    FCGX_FPrintF(message.out, "Content-Type: %s\r\n", contentType.c_str());
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(message.out, "Content-Length: %d\r\n", (int)body.size());
    FCGX_FPrintF(message.out, "\r\n");
    FCGX_PutStr(body.data(), body.size(), message.out);
}

void HTTP_ResponseDataAsChunkBinaries(
    FCGX_Request& message, 
    int code, 
    const std::string& contentType, 
    const std::string& body, 
    long long start, 
    long long end, 
    long long total) {
    /**/
    FCGX_FPrintF(message.out, "Status: %d %s\r\n", code, HTTP_StatusReturn(code));
    FCGX_FPrintF(message.out, "Content-Type: %s\r\n", contentType.c_str());
    FCGX_FPrintF(message.out, "Accept-Ranges: bytes\r\n");
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    if (code == 206) {
        FCGX_FPrintF(message.out, "Content-Range: bytes %lld-%lld/%lld\r\n", start, end, total);
    }
    FCGX_FPrintF(message.out, "Content-Length: %lld\r\n", (long long)body.size());
    FCGX_FPrintF(message.out, "\r\n");
    FCGX_PutStr(body.data(), body.size(), message.out);
}

void HTTP_ResponseRangeNotSatisfiable(
    FCGX_Request& message, 
    long long total) {
    /**/
    FCGX_FPrintF(message.out, "Status: 416 Range Not Satisfiable\r\n");
    FCGX_FPrintF(message.out, "Content-Range: bytes */%d\r\n", total);
    FCGX_FPrintF(message.out, "Content-Length: 0\r\n");
    FCGX_FPrintF(message.out, "\r\n");
}

size_t HTTP_ExtractBodyContentLength(FCGX_Request& message) {
    size_t bodyLength = std::atoi(stGetEnvirVariables(message, "CONTENT_LENGTH").c_str());
    return bodyLength;
}

std::string HTTP_ExtractBodyContent(FCGX_Request& message) {
    int len = std::atoi(stGetEnvirVariables(message, "CONTENT_LENGTH").c_str());
    if (len <= 0) {
        return "";
    }
    if (len > 4095) {
        len = 4095;
    }
    std::string body(len, '\0');
    int n = FCGX_GetStr(&body[0], len, message.in);
    body.resize(n > 0 ? n : 0);
    return body;
}

void HTTP_DecodeSubmitForm(char *src, char *dst) {
    char a, b;
    while (*src) {
        if ((*src == '%') && isxdigit(src[1]) && isxdigit(src[2])) {
            a = src[1];
            b = src[2];
            a = (a >= 'a') ? a - 'a' + 10 : (a >= 'A') ? a - 'A' + 10 : a - '0';
            b = (b >= 'a') ? b - 'a' + 10 : (b >= 'A') ? b - 'A' + 10 : b - '0';
            *dst++ = 16 * a + b;
            src += 3;
        }
		else if (*src == '+') {
            *dst++ = ' ';
            src++;
        }
		else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static std::string HTTP_GetCookieValue(
    const std::string& cookieHeader,
    const std::string& name) {
    /**/
    size_t pos = 0;
    while (pos < cookieHeader.size()) {
        while (pos < cookieHeader.size() && (cookieHeader[pos] == ' ' || cookieHeader[pos] == ';')) {
            ++pos;
        }

        size_t eq = cookieHeader.find('=', pos);
        if (eq == std::string::npos) {
            break;
        }

        size_t end = cookieHeader.find(';', eq + 1);
        if (end == std::string::npos) {
            end = cookieHeader.size();
        }

        std::string key = cookieHeader.substr(pos, eq - pos);
        if (key == name) {
            return cookieHeader.substr(eq + 1, end - eq - 1);
        }

        pos = end + 1;
    }
    return "";
}

bool HTTP_IsAuthenticated(FCGX_Request& message, int *role) {
    bool success = false;
    /*  Let check JWT token from:
        Cookie Authorization Header 
    */
    std::string token = HTTP_GetCookieValue(stGetEnvirVariables(message, "HTTP_COOKIE"), JWT_AUTHORISE_SESSION);
    if (!token.empty()) {
        bool success = jwt_authorise_validate_token(token);
        if (success && role) {
            *role = jwt_authorise_get_role(token);
        }
        return success;
    }
    /* Check JWT from Authorization: Bearer <token> */
    const std::string bearer = "Bearer ";
    const std::string authorization = stGetEnvirVariables(message, "HTTP_AUTHORIZATION");

    if (authorization.compare(0, bearer.size(), bearer) != 0) {
        return false;
    }
    token = authorization.substr(bearer.size());
    if (token.empty()) {
        return false;
    }
    success = jwt_authorise_validate_token(token);
    if (success && role) {
        *role = jwt_authorise_get_role(token);
    }
    return success;
}

std::string HTTP_GenerateCookies(const std::string& username, int role) {
    std::string jwt = jwt_authorise_generate_token(username, role);
    std::string cookie = std::string("Set-Cookie: ") + JWT_AUTHORISE_SESSION + "=" +
                         jwt +
                         "; Path=/; Max-Age=" + std::to_string(JWT_AUTHORISE_EXPIRED_SECONDS) + 
                         "; HttpOnly; SameSite=Strict";
    return cookie;
}

bool HTTP_ExtractRangeHeader(
    const std::string& range, 
    long long total, 
    long long& start, 
    long long& end) {
    /**/
    const std::string prefix = "bytes=";
    if (range.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    std::string spec = range.substr(prefix.size());
    size_t dash = spec.find('-');
    if (dash == std::string::npos) {
        return false;
    }

    std::string startText = spec.substr(0, dash);
    std::string endText = spec.substr(dash + 1);

    if (startText.empty()) {
        long long suffix = std::atoll(endText.c_str());
        if (suffix <= 0) return false;
        start = suffix > total ? 0 : total - suffix;
        end = total - 1;
        return true;
    }

    start = std::atoll(startText.c_str());
    end = endText.empty() ? total - 1 : std::atoll(endText.c_str());

    return start >= 0 && end >= start && start < total;
}