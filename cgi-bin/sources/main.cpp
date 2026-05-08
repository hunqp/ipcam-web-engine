#include <string.h>

#include "main.h"
#include "kiwi_log.h"

#define WEB_ROOT        "/home/hunqp/EPCB/WebEngine/envir/www"
#define AUTH_COOKIE     "camera_auth"
#define JWT_SECRET      "change-this-camera-jwt-secret"
#define SESSION_EXPIRED_SECONDS 3600

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::string stGetEnvirVariables(FCGX_Request& request, const char* name) {
    const char* v = FCGX_GetParam(name, request.envp);
    return (v) ? std::string(v) : "" /* Env variables can be empty string, avoid nullptr */;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main() {
    FCGX_Request message;
    Kiwi_Journal.filename = "/home/hunqp/EPCB/WebEngine/envir/apis.log";

    FCGX_Init();
    FCGX_InitRequest(&message, 0, 0);

    while (FCGX_Accept_r(&message) >= 0) {
        std::string uri = stGetEnvirVariables(message, (const char*)"REQUEST_URI");
        std::string method = stGetEnvirVariables(message, (const char*)"REQUEST_METHOD");
        size_t param = uri.find('?');
        std::string path = uri.substr(0, param);

        VV_SYSD("Method: %s\r\n", method.c_str());
        VV_SYSD("URI   : %s\r\n", uri.c_str());
        VV_SYSD("Path  : %s\r\n", path.c_str());

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
                VV_SYSD("API: %s\r\n", selected[index].api);
                if (boolean) {
                    selected[index].callback(message);
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
