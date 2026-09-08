#include <string.h>

#include "main.h"
#include "base64.h"
#include "streamer.h"
#include "cgi_debug.h"
#include "helpers.h"

std::string WWW_ROOT;
bool IS_MACHINE_UPGRADING = false;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::string stGetEnvirVariables(FCGX_Request& request, const char* name) {
    const char* v = FCGX_GetParam(name, request.envp);
    return (v) ? std::string(v) : "" /* Env variables can be empty string, avoid nullptr */;
}

static inline void prepare() {
    #define PROG_API_DIARY  RAM_ROOT "/fcgi-api.log"

    CGI_FLASH.filename = (const char*)PROG_API_DIARY;
    rk_param_init((char*)APP_IPC_CONFIGURE_FILE);

    /* Initialise secret by serial number of device */
    Kiwi_Credentials_Setup(APP_ACCOUNTS_DB_FILE, APP_SECRET_UNIQUE_FILE);

    /* Derive this process' JWT signing secret (see authorise.cpp). Must run
     * before InitStreamer(), whose WebSocket auth hook validates tokens too. */
    jwt_authorise_setup();

    /* Auto generate password default for the first time */
    if (access(APP_ACCOUNTS_DB_FILE, F_OK) != 0) {
        KIWI_CREDENTIALS_T defaultCredentials = {0};
        const char defaultUsername[] = "admin";
        char defaultPassword[9] = {0};

        /**
         * Generate salt by MAC address with ffde prefix, 
         * then generate password by SHA-256(salt)
         */
        char salt[32] = {0};
        uint8_t MAC[6] = {0};
        Kiwi_MAC_GetAddress("eth0", MAC, NULL);
        snprintf(salt, sizeof(salt), "%02x%02x%02x%02x%02x%02xffde", MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5]);
        Kiwi_Credentials_GeneratePassword(salt, defaultPassword, sizeof(defaultPassword));
        CGI_SYSD("%s:%s\r\n", defaultUsername, defaultPassword);
        strcpy(defaultCredentials.username, defaultUsername);
        strcpy(defaultCredentials.password, defaultPassword);
        defaultCredentials.role = 0; /* Administrator */
        Kiwi_Credentials_Add(&defaultCredentials);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main() {
    prepare();

    FCGX_Request message;
    FCGX_Init();
    FCGX_InitRequest(&message, 0, 0);
    InitStreamer();
    WWW_ROOT = std::string(getenv("ENVIR_DIR")) + "/www";

    while (FCGX_Accept_r(&message) >= 0) {
        /**
         * IGNORE all request if the machine is UPGRADING
         */
        if (IS_MACHINE_UPGRADING) {
            HTTP_ResponseDataAsJSON(message, 403, "{\"success\": false, \"message\": \"MACHINE IS UPGRADING\"}");
            FCGX_Finish_r(&message);
            continue;
        }

        std::string uri = stGetEnvirVariables(message, (const char*)"REQUEST_URI");
        std::string method = stGetEnvirVariables(message, (const char*)"REQUEST_METHOD");
        size_t param = uri.find('?');
        std::string path = uri.substr(0, param);
        std::string query = (param != std::string::npos) ? uri.substr(param + 1) : "";
        
        CGI_SYSD("CGI -> Method: \'%s\', Path: \'%s\'\r\n", method.c_str(), path.c_str());

        struct RouteMap {
            const char *method;
            HashTableEntrance *routes;
        };
        RouteMap maps[] = {
            { "GET"     , GET_HashMap       },
            { "PUT"     , PUT_HashMap       },
            { "POST"    , POST_HashMap      },
            { "UPDATE"  , UPDATE_HashMap    },
            { "DELETE"  , DELETE_HashMap    }
        };
        HashTableEntrance *selected = NULL;
        /* Find all method that supports */
        for (auto &p : maps) {
            if (p.method == method) {
                selected = p.routes;
                break;
            }
        }
        /* Find all path support */
        if (selected) {
            int index = 0;
            bool hasFound = false;
            for (uint8_t id = 0; selected[id].api != NULL; ++id) {
                char *api = selected[id].api;
                if (strcmp(api, path.c_str()) == 0) {
                    hasFound = true;
                    index = id;
                    break;
                }
            }
            if (hasFound) {
                bool boolean = true;
                eUserLevels role = Customer; /* Least privilege until authenticated */
                if (selected[index].needToAuthenticate) {
                    /**
                     * Authentication is accepted ONLY via the session cookie
                     * (vivoo_session) or an "Authorization: Bearer <jwt>" header,
                     * both handled by HTTP_IsAuthenticated().
                     *
                     * Credentials in the query string are intentionally NOT
                     * accepted anymore: they leaked into the process log, the
                     * web-server access log, proxies and browser history, and
                     * that path also bypassed the per-IP brute-force lockout
                     * that guards POST /api/v1/user/login.
                     */
                    boolean = HTTP_IsAuthenticated(message, (int*)&role);
                    if (!boolean) {
                        HTTP_ResponseDataAsJSON(message, 401, "{\"success\": false, \"message\": \"Unauthorized\"}");
                    } else if (role > selected[index].role) {
                        /* Permissions verification (smaller enum value == more privilege) */
                        HTTP_ResponseDataAsJSON(message, 403, "{\"success\": false, \"message\": \"Forbidden: Permissions denied\"}");
                        boolean = false;
                    }
                }
                if (boolean) {
                    /*
                        @JSON Template response
                        {
                            "success": true,
                            "message": "Accepted",
                            "timestamp": 1577836800,
                            "data": {}
                        }
                    */
                    nlohmann::json js;
                    js["success"] = true;
                    js["message"] = "Accepted";
                    js["timestamp"] = (uint32_t)time(NULL);
                    js["data"] = nlohmann::json::object();
                    try {
						CGI_SYSD("Selected: %s\r\n", selected[index].api);
                        /* Reload parameter when GET */
                        if (method == "GET") {
                            rk_param_reload();
                        }
                        selected[index].callback(message, js);
                    }
                    catch (const nlohmann::json::parse_error& e) {
                        js["success"] = false;
                        js["message"] = "Invalid JSON format: " + std::string(e.what());
                        HTTP_ResponseDataAsJSON(message, 400, js.dump());
                    }
                    catch (const std::exception& e) {
                        js["success"] = false;
                        js["message"] = "Server runtime error: " + std::string(e.what());
                        HTTP_ResponseDataAsJSON(message, 500, js.dump());
                    }
                    catch (...) {
                        js["success"] = false;
                        js["message"] = "Unknown internal server error";
                        HTTP_ResponseDataAsJSON(message, 500, js.dump());
                    }
                }
            }
            else if (method == "GET" && path.compare(0, 9, "/records/") == 0) {
                /**
                 * Recorded clips on the SD card. These used to be served with
                 * NO authentication at all through a lighttpd
                 * 'alias.url = ( "/records/" => "/mnt/sdcard/" )' mapping.
                 * They are now routed through FastCGI so a valid session is
                 * required before any file is returned.
                 */
                eUserLevels role = Customer;
                if (!HTTP_IsAuthenticated(message, (int*)&role)) {
                    CGI_SYSW("Record download unauthorized: %s\r\n", path.c_str());
                    HTTP_ResponseDataAsJSON(message, 401, "{\"success\": false, \"message\": \"Unauthorized\"}");
                } else {
                    extern void HTTP_ServeRecordFile(FCGX_Request& message, const std::string& urlPath);
                    HTTP_ServeRecordFile(message, path);
                }
            }
            else {
                /* Not Found */
                HTTP_ResponseDataAsHTML(message, 404, "<h1>404</h1>");
            }
        }
        else {
            /* Method Not Allowed */
            HTTP_ResponseDataAsHTML(message, 405, "<h1>405</h1>");
        }

        FCGX_Finish_r(&message);
    }

    return 0;
}
