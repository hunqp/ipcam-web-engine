#include <cstdlib>
#include <ctime>
#include <cstring>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <chrono>

#include "fcgiapp.h"
#include "json.hpp"
#include "jwt/jwt.h"
#include "kiwi_log.h"
#include "http_utils.h"

#define WEB_ROOT "/home/hunqp/EPCB/WebEngine/envir/www"
#define LIVE_VIDEO_FILE "livestream.mp4"
#define AUTH_COOKIE "camera_auth"
#define JWT_SECRET "change-this-camera-jwt-secret"
#define SESSION_EXPIRED_SECONDS 3600

struct User {
    std::string username;
    std::string password;
};

struct PortSettings {
    int http;
    int rtsp;
    int https;
    int server;
};

static User users[] = {
    {"admin", "admin"}
};

static PortSettings portSettings = {
    80,
    1554,
    443,
    8000
};

static std::string getEnv(FCGX_Request& req, const char* name)
{
    const char* v = FCGX_GetParam(name, req.envp);
    return v ? v : "";
}

static std::string readFile(const std::string& path)
{
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        return "";
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string readBinaryFile(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static bool getFileSize(const std::string& path, long long& size)
{
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    size = (long long)file.tellg();
    return size >= 0;
}

static bool readBinaryRange(const std::string& path,
                            long long start,
                            long long length,
                            std::string& out)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open() || start < 0 || length < 0) {
        return false;
    }

    file.seekg(start, std::ios::beg);
    out.assign((size_t)length, '\0');
    file.read(&out[0], length);
    out.resize((size_t)file.gcount());

    return file.good() || file.eof();
}

static std::string readBody(FCGX_Request& req)
{
    int len = std::atoi(getEnv(req, "CONTENT_LENGTH").c_str());
    if (len <= 0) return "";
    if (len > 8192) len = 8192;

    std::string body(len, '\0');
    int n = FCGX_GetStr(&body[0], len, req.in);
    body.resize(n > 0 ? n : 0);

    return body;
}

static std::map<std::string, std::string> parseFormData(const std::string& data)
{
    std::map<std::string, std::string> result;

    size_t pos = 0;
    while (pos < data.size()) {
        size_t eq = data.find('=', pos);
        if (eq == std::string::npos) break;

        size_t amp = data.find('&', eq);
        if (amp == std::string::npos) amp = data.size();

        std::string key = data.substr(pos, eq - pos);
        std::string value = data.substr(eq + 1, amp - eq - 1);

        auto decode = [](const std::string& input) {
            std::string output;
            output.reserve(input.size());

            for (size_t i = 0; i < input.size(); ++i) {
                if (input[i] == '+') {
                    output += ' ';
                } else if (input[i] == '%' && i + 2 < input.size()) {
                    char hex[3] = { input[i + 1], input[i + 2], '\0' };
                    char* end = nullptr;
                    long decoded = std::strtol(hex, &end, 16);
                    if (end && *end == '\0') {
                        output += static_cast<char>(decoded);
                        i += 2;
                    } else {
                        output += input[i];
                    }
                } else {
                    output += input[i];
                }
            }

            return output;
        };

        result[decode(key)] = decode(value);
        pos = amp + 1;
    }

    return result;
}

static bool validateCredentials(const std::string& username,
                                const std::string& password)
{
    for (const auto& user : users) {
        if (user.username == username && user.password == password) {
            return true;
        }
    }
    return false;
}

static std::string makeJwt(const std::string& username)
{
    auto now = std::chrono::system_clock::now();

    return jwt::create()
        .set_type("JWT")
        .set_subject(username)
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(SESSION_EXPIRED_SECONDS))
        .sign(jwt::algorithm::hs256(JWT_SECRET));
}

static bool validateJwtToken(const std::string& token)
{
    try {
        auto decoded = jwt::decode(token);
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256(JWT_SECRET))
            .verify(decoded);
        return true;
    } catch (const std::exception& e) {
        VV_SYSD("JWT invalid: %s\r\n", e.what());
        return false;
    }
}

static std::string getCookieValue(const std::string& cookieHeader,
                                  const std::string& name)
{
    size_t pos = 0;
    while (pos < cookieHeader.size()) {
        while (pos < cookieHeader.size() &&
               (cookieHeader[pos] == ' ' || cookieHeader[pos] == ';')) {
            ++pos;
        }

        size_t eq = cookieHeader.find('=', pos);
        if (eq == std::string::npos) break;

        size_t end = cookieHeader.find(';', eq + 1);
        if (end == std::string::npos) end = cookieHeader.size();

        std::string key = cookieHeader.substr(pos, eq - pos);
        if (key == name) {
            return cookieHeader.substr(eq + 1, end - eq - 1);
        }

        pos = end + 1;
    }

    return "";
}

static bool isAuthenticated(FCGX_Request& req)
{
    std::string token = getCookieValue(getEnv(req, "HTTP_COOKIE"), AUTH_COOKIE);
    if (!token.empty()) {
        return validateJwtToken(token);
    }

    std::string auth = getEnv(req, "HTTP_AUTHORIZATION");
    const std::string bearer = "Bearer ";
    if (auth.compare(0, bearer.size(), bearer) == 0) {
        return validateJwtToken(auth.substr(bearer.size()));
    }

    return false;
}

static const char* statusText(int status) {
    switch (status) {
        case 200: return (const char*)"OK";
        case 206: return (const char*)"Partial Content";
        case 302: return (const char*)"Found";
        case 400: return (const char*)"Bad Request";
        case 401: return (const char*)"Unauthorized";
        case 404: return (const char*)"Not Found";
        case 405: return (const char*)"Method Not Allowed";
        case 416: return (const char*)"Range Not Satisfiable";
        case 500: return (const char*)"Internal Server Error";
        default:  return (const char*)"OK";
    }
}

static void sendJson(FCGX_Request& req, int status, const std::string& message,
                     const std::string& extraHeader = "") {
    FCGX_FPrintF(req.out, "Status: %d %s\r\n", status, statusText(status));
    FCGX_FPrintF(req.out, "Content-Type: application/json\r\n");
    FCGX_FPrintF(req.out, "Cache-Control: no-store\r\n");
    if (!extraHeader.empty()) {
        FCGX_FPrintF(req.out, "%s\r\n", extraHeader.c_str());
    }
    FCGX_FPrintF(req.out, "Content-Length: %d\r\n", (int)message.size());
    FCGX_FPrintF(req.out, "\r\n");
    FCGX_FPrintF(req.out, "%s", message.c_str());
}

static void sendHtml(FCGX_Request& req, int status, const std::string& html)
{
    FCGX_FPrintF(req.out, "Status: %d %s\r\n", status, statusText(status));
    FCGX_FPrintF(req.out, "Content-Type: text/html; charset=utf-8\r\n");
    FCGX_FPrintF(req.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(req.out, "Content-Length: %d\r\n", (int)html.size());
    FCGX_FPrintF(req.out, "\r\n");
    FCGX_FPrintF(req.out, "%s", html.c_str());
}

static void sendBinary(FCGX_Request& req,
                       int status,
                       const std::string& contentType,
                       const std::string& body)
{
    FCGX_FPrintF(req.out, "Status: %d %s\r\n", status, statusText(status));
    FCGX_FPrintF(req.out, "Content-Type: %s\r\n", contentType.c_str());
    FCGX_FPrintF(req.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(req.out, "Content-Length: %d\r\n", (int)body.size());
    FCGX_FPrintF(req.out, "\r\n");
    FCGX_PutStr(body.data(), body.size(), req.out);
}

static void sendVideoChunk(FCGX_Request& req,
                           int status,
                           const std::string& body,
                           long long start,
                           long long end,
                           long long total)
{
    FCGX_FPrintF(req.out, "Status: %d %s\r\n", status, statusText(status));
    FCGX_FPrintF(req.out, "Content-Type: video/mp4\r\n");
    FCGX_FPrintF(req.out, "Accept-Ranges: bytes\r\n");
    FCGX_FPrintF(req.out, "Cache-Control: no-store\r\n");
    if (status == 206) {
        FCGX_FPrintF(req.out, "Content-Range: bytes %lld-%lld/%lld\r\n", start, end, total);
    }
    FCGX_FPrintF(req.out, "Content-Length: %lld\r\n", (long long)body.size());
    FCGX_FPrintF(req.out, "\r\n");
    FCGX_PutStr(body.data(), body.size(), req.out);
}

static void sendRangeNotSatisfiable(FCGX_Request& req, long long total)
{
    FCGX_FPrintF(req.out, "Status: 416 Range Not Satisfiable\r\n");
    FCGX_FPrintF(req.out, "Content-Range: bytes */%lld\r\n", total);
    FCGX_FPrintF(req.out, "Content-Length: 0\r\n");
    FCGX_FPrintF(req.out, "\r\n");
}

static void sendRedirect(FCGX_Request& req, const std::string& location)
{
    FCGX_FPrintF(req.out, "Status: 302 Found\r\n");
    FCGX_FPrintF(req.out, "Location: %s\r\n", location.c_str());
    FCGX_FPrintF(req.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(req.out, "Content-Length: 0\r\n");
    FCGX_FPrintF(req.out, "\r\n");
}


static void handleLogin(FCGX_Request& req, const std::string& method)
{
    if (method != "POST") {
        sendJson(req, 405, "{\"ok\":false,\"error\":\"Method not allowed\"}");
        return;
    }

    std::string body = readBody(req);
    VV_SYSD("Login: %s\r\n", body.c_str());
    auto form = parseFormData(body);

    std::string username = form["username"];
    std::string password = form["password"];

    if (validateCredentials(username, password)) {
        std::string jwt = makeJwt(username);
        std::string cookie = std::string("Set-Cookie: ") + AUTH_COOKIE + "=" +
                             jwt +
                             "; Path=/; Max-Age=3600; HttpOnly; SameSite=Strict";

        VV_SYSD("Cookie: %s\r\n", cookie.c_str());                             
        sendJson(req, 200, "{\"ok\":true,\"redirect\":\"/home.html\",\"token\":\"" + jwt + "\"}", cookie);
    } else {
        sendJson(req, 401, "{\"ok\":false,\"error\":\"Invalid username or password\"}");
    }
}

static void handleLogout(FCGX_Request& req, const std::string& method)
{
    if (method != "POST") {
        sendJson(req, 405, "{\"ok\":false,\"error\":\"Method not allowed\"}");
        return;
    }

    std::string cookie = std::string("Set-Cookie: ") + AUTH_COOKIE +
                         "=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly; SameSite=Strict";

    sendJson(req, 200, "{\"ok\":true,\"redirect\":\"/index.html\"}", cookie);
}

static void handleHome(FCGX_Request& req, const std::string& method)
{
    if (method != "GET") {
        sendJson(req, 405, "{\"ok\":false,\"error\":\"Method not allowed\"}");
        return;
    }

    if (!isAuthenticated(req)) {
        sendRedirect(req, "/index.html");
        return;
    }

    std::string html = readFile(std::string(WEB_ROOT) + "/home.html");
    if (html.empty()) {
        sendJson(req, 404, "{\"ok\":false,\"error\":\"home page not found\"}");
        return;
    }

    sendHtml(req, 200, html);
}

static void handleSnapshot(FCGX_Request& req, const std::string& method)
{
    if (method != "GET") {
        sendJson(req, 405, "{\"ok\":false,\"error\":\"Method not allowed\"}");
        return;
    }

    if (!isAuthenticated(req)) {
        sendJson(req, 401, "{\"ok\":false,\"error\":\"Unauthorized\"}");
        return;
    }

    std::string image = readBinaryFile(std::string(WEB_ROOT) + "/test.png");
    if (image.empty()) {
        sendJson(req, 404, "{\"ok\":false,\"error\":\"snapshot not found\"}");
        return;
    }

    sendBinary(req, 200, "image/png", image);
}

static bool parseRangeHeader(const std::string& range,
                             long long total,
                             long long& start,
                             long long& end)
{
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

static void handleLiveVideo(FCGX_Request& req, const std::string& method)
{
    if (method != "GET") {
        sendJson(req, 405, "{\"ok\":false,\"error\":\"Method not allowed\"}");
        return;
    }

    if (!isAuthenticated(req)) {
        sendJson(req, 401, "{\"ok\":false,\"error\":\"Unauthorized\"}");
        return;
    }

    std::string path = std::string(WEB_ROOT) + "/" + LIVE_VIDEO_FILE;
    long long total = 0;
    if (!getFileSize(path, total) || total <= 0) {
        sendJson(req, 404, "{\"ok\":false,\"error\":\"video not found\"}");
        return;
    }

    long long start = 0;
    long long end = total - 1;
    int status = 200;
    std::string range = getEnv(req, "HTTP_RANGE");
    if (!range.empty()) {
        if (!parseRangeHeader(range, total, start, end)) {
            sendRangeNotSatisfiable(req, total);
            return;
        }
        status = 206;
    }

    if (end >= total) {
        end = total - 1;
    }

    std::string video;
    if (!readBinaryRange(path, start, end - start + 1, video)) {
        sendJson(req, 500, "{\"ok\":false,\"error\":\"can not read video\"}");
        return;
    }

    sendVideoChunk(req, status, video, start, start + (long long)video.size() - 1, total);
}

static bool parsePortValue(const nlohmann::json& body,
                           const char* key,
                           int& value)
{
    if (!body.contains(key) || !body[key].is_number_integer()) {
        return false;
    }

    int parsed = body[key].get<int>();
    if (parsed < 1 || parsed > 65535) {
        return false;
    }

    value = (int)parsed;
    return true;
}

static void handlePortSettings(FCGX_Request& req, const std::string& method)
{
    if (method != "POST") {
        sendJson(req, 405, "{\"ok\":false,\"error\":\"Method not allowed\"}");
        return;
    }

    if (!isAuthenticated(req)) {
        sendJson(req, 401, "{\"ok\":false,\"error\":\"Unauthorized\"}");
        return;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(readBody(req));
    } catch (...) {
        sendJson(req, 400, "{\"ok\":false,\"error\":\"Invalid JSON body\"}");
        return;
    }

    PortSettings next = portSettings;
    if (!parsePortValue(body, "http", next.http) ||
        !parsePortValue(body, "rtsp", next.rtsp) ||
        !parsePortValue(body, "https", next.https) ||
        !parsePortValue(body, "server", next.server)) {
        sendJson(req, 400, "{\"ok\":false,\"error\":\"Port range must be 1 to 65535\"}");
        return;
    }

    portSettings = next;
    VV_SYSD("Port settings saved: http=%d rtsp=%d https=%d server=%d\r\n",
            portSettings.http,
            portSettings.rtsp,
            portSettings.https,
            portSettings.server);

    std::string response = "{\"ok\":true,\"message\":\"Port settings saved\",\"ports\":{"
                           "\"http\":" + std::to_string(portSettings.http) +
                           ",\"rtsp\":" + std::to_string(portSettings.rtsp) +
                           ",\"https\":" + std::to_string(portSettings.https) +
                           ",\"server\":" + std::to_string(portSettings.server) +
                           "}}";
    sendJson(req, 200, response);
}

int main() {
    Kiwi_Journal.filename = "/home/hunqp/EPCB/WebEngine/envir/apis.log";

    FCGX_Init();

    FCGX_Request req;
    FCGX_InitRequest(&req, 0, 0);

    while (FCGX_Accept_r(&req) >= 0) {
        std::string method = getEnv(req, "REQUEST_METHOD");
        std::string uri = getEnv(req, "REQUEST_URI");

        size_t q = uri.find('?');
        std::string path = uri.substr(0, q);

        VV_SYSD("Method: %s\r\n", method.c_str());
        VV_SYSD("URI   : %s\r\n", uri.c_str());
        VV_SYSD("Path  : %s\r\n", path.c_str());

        if (path != "/api/v1/authen/login" &&
            path.compare(0, 8, "/api/v1/") == 0 &&
            !isAuthenticated(req)) {
            sendJson(req, 401, "{\"ok\":false,\"error\":\"Unauthorized\"}");
            FCGX_Finish_r(&req);
            continue;
        }

        if (path == "/api/v1/authen/login") {
            handleLogin(req, method);
        }
        else if (path == "/api/v1/authen/logout") {
            handleLogout(req, method);
        }
        else if (path == "/home.html") {
            handleHome(req, method);
        }
        else if (path == "/api/v1/media/snapshot") {
            handleSnapshot(req, method);
        }
        else if (path == "/api/v1/live/video") {
            handleLiveVideo(req, method);
        }
        else if (path == "/api/v1/network/ports") {
            handlePortSettings(req, method);
        }
        else if (path == "/api/v1/stream/request") {
            sendJson(req, 200, "{\"ok\":true,\"api\":\"stream/request\"}");
        }
        else if (path == "/api/v1/stream/answer") {
            sendJson(req, 200, "{\"ok\":true,\"api\":\"stream/answer\"}");
        }
        else if (path == "/api/v1/device/reboot") {
            sendJson(req, 200, "{\"ok\":true,\"message\":\"reboot requested\"}");
        }
        else if (path == "/api/v1/device/reset") {
            sendJson(req, 200, "{\"ok\":true,\"message\":\"reset requested\"}");
        }
        else if (path == "/api/v1/media/video") {
            sendJson(req, 200, "{\"ok\":true,\"api\":\"media/video\"}");
        }
        else if (path == "/api/v1/media/audio") {
            sendJson(req, 200, "{\"ok\":true,\"api\":\"media/audio\"}");
        }
        else if (path == "/api/v1/media/image") {
            sendJson(req, 200, "{\"ok\":true,\"api\":\"media/image\"}");
        }
        else if (path == "/api/v1/control/led") {
            sendJson(req, 200, "{\"ok\":true,\"api\":\"control/led\"}");
        }
        else if (path == "/api/v1/control/motors") {
            sendJson(req, 200, "{\"ok\":true,\"api\":\"control/motors\"}");
        }
        else {
            sendJson(req, 404, "{\"ok\":false,\"error\":\"unknown api\"}");
        }
        FCGX_Finish_r(&req);
    }

    return 0;
}
