#ifndef MAIN_H
#define MAIN_H

#include "utils.h"
#include "kiwi_log.h"
#include "json.hpp"
#include "fcgiapp.h"

#include <string>
#include <unordered_map>

#define WWW_ROOT                ENVIR_ROOT_DIR "/www"
#define WWW_LOGIN_REDIRECT      "/login"
#define WWW_PREVIEW_REDIRECT    "/preview"
#define WWW_INDEX_PAGE          "/index.html"
#define WWW_PREVIEW_PAGE        "/preview.html"
#define WWW_IMAGE_JPEG          "/image.jpeg"
#define WWW_VIDEO_MP4           "/livestream.mp4"

typedef void (*CGI_FunCallback)(FCGX_Request& message);

typedef struct {
    char *api;
    bool needToAuthenticate;
    CGI_FunCallback callback;
} HashTableEntrance;

extern HashTableEntrance GET_HashMap[];
extern HashTableEntrance PUT_HashMap[];
extern HashTableEntrance POST_HashMap[];
extern HashTableEntrance UPDATE_HashMap[];
extern HashTableEntrance DELETE_HashMap[];

extern void HTTP_ResponseRedirect(FCGX_Request& message, const std::string& place);
extern void HTTP_ResponseDataAsJSON(FCGX_Request& message, int code, const std::string& content, const std::string& extraHeader = "");
extern void HTTP_ResponseDataAsHTML(FCGX_Request& message, int code, const std::string& html);
extern void HTTP_ResponseDataAsBinaries(FCGX_Request& message, int code, const std::string& contentType, const std::string& body);

extern std::string HTTP_ExtractBodyContent(FCGX_Request& message);
extern void HTTP_DecodeSubmitForm(char *src, char *dest);
extern bool HTTP_IsAuthenticated(FCGX_Request& message);
extern std::string HTTP_GenerateCookies(const std::string& username);

#endif /* MAIN_H */

// {
//     "success": true,
//     "message": "Get audio configuration returns success",
//     "timestmap": 1778171000,
//     "data": {
//     "microphone": {
//         "volume": 100,
//         "gain": 20,
//         "echo_cancellation": false,
//         "noise_suppression": false
//     },
//     "speaker": {
//         "volume": 100,
//         "software_amplifier": false,
//         "software_amplifier_value": 1.5
//     }
//     }
// }

// {
//     "Success": true,
//     "Message": "Get audio configuration returns success",
//     "Timestmap": 1778171000,
//     "Data": {
//         "Microphone": {
//             "Volume": 100,
//             "Gain": 20,
//             "EchoCancellation": false,
//             "NoiseSuppression": false
//         },
//         "Speaker": {
//             "Volume": 100,
//             "SoftwareAmplifier": false,
//             "SoftwareAmplifierValue": 1.5
//         }
//     }
// }