#include <string.h>

#include "main.h"
#include "base64.h"
#include "streamer.h"
#include "cgi_debug.h"
#include "http_utils.h"


std::string WWW_ROOT;

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
    std::string secret = readFile(APP_SECRET_UNIQUE_FILE);
    Kiwi_Credentials_Setup(APP_ACCOUNTS_DB_FILE, secret.c_str());

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
        std::string uri = stGetEnvirVariables(message, (const char*)"REQUEST_URI");
        std::string method = stGetEnvirVariables(message, (const char*)"REQUEST_METHOD");
        size_t param = uri.find('?');
        std::string path = uri.substr(0, param);
        
        CGI_SYSD("CGI -> Method: \'%s\', URI: \'%s\', Path: \'%s\'\r\n", method.c_str(), uri.c_str(), path.c_str());

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
        /* Find all method that supports*/
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
                eUserLevels role;
                bool boolean = true;
                if (selected[index].needToAuthenticate) {
                    boolean = HTTP_IsAuthenticated(message, (int*)&role);
                    /* Authorise verification */
                    if (!boolean) {
                        HTTP_ResponseDataAsJSON(message, 401, "{\"success\": false, \"message\": \"Unauthorized\"}");
                    } else {
                        /* Permissions verification */
                        if (role > selected[index].role) {
                            HTTP_ResponseDataAsJSON(message, 403, "{\"success\": false, \"message\": \"Forbidden: Permissions denied\"}");
                            boolean = false;
                        }
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
