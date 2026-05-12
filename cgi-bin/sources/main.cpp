#include <string.h>

#include "main.h"
#include "cgi_debug.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::string stGetEnvirVariables(FCGX_Request& request, const char* name) {
    const char* v = FCGX_GetParam(name, request.envp);
    return (v) ? std::string(v) : "" /* Env variables can be empty string, avoid nullptr */;
}

static inline void prepare() {
    snprintf(
        (char*)CGI_FLASH.filename, 
        sizeof(CGI_FLASH.filename), 
        "%s/../apis.log", 
        WWW_ROOT
    );

    char *RK_PARAM_PATH = (char*)"/userdata/rkipc.ini";
    rk_param_init(RK_PARAM_PATH);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main() {
    FCGX_Request message;
    FCGX_Init();
    FCGX_InitRequest(&message, 0, 0);

    while (FCGX_Accept_r(&message) >= 0) {
        std::string uri = stGetEnvirVariables(message, (const char*)"REQUEST_URI");
        std::string method = stGetEnvirVariables(message, (const char*)"REQUEST_METHOD");
        size_t param = uri.find('?');
        std::string path = uri.substr(0, param);

        CGI_SYSD("Method: %s\r\n", method.c_str());
        CGI_SYSD("URI   : %s\r\n", uri.c_str());
        CGI_SYSD("Path  : %s\r\n", path.c_str());

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
                bool boolean = true;
                if (selected[index].needToAuthenticate) {
                    boolean = HTTP_IsAuthenticated(message);
                    if (!boolean) {
                        HTTP_ResponseDataAsJSON(message, 401, "{\"success\": false, \"message\": \"Unauthorized\"}");
                    }
                } 
                CGI_SYSD("API: %s\r\n", selected[index].api);
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
                        js["message"] = "Unknown internal server error.";
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
