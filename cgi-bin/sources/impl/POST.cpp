#include "main.h"
#include "utils.h"
#include "http_utils.h"

extern std::string stGetEnvirVariables(FCGX_Request& request, const char* name);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool validateCredentials(
    const std::string& username,
    const std::string& password) {
    /**/
    /* TODO: Read credentials from database */
    if (username == "admin" && password == "admin") {
        return true;
    }
    return false;
}

static void APIV1_CGI_UserLogin(FCGX_Request& message) {
    int status = 401;
    nlohmann::json js;
    char username[32] = {0};
	char password[32] = {0};
    std::string extraHeader;
    std::string body = HTTP_ExtractBodyContent(message);

	/* %127[^&] means read up to 127 characters */
	if (sscanf(body.c_str(), "username=%127[^&]&password=%127s", username, password) == 2) {
        HTTP_DecodeSubmitForm(username, username);
        HTTP_DecodeSubmitForm(password, password);
        if (validateCredentials(username, password)) {
            status = 200;
            extraHeader = HTTP_GenerateCookies(username);
        }
	}

    if (status == 200) {
        js["success"] = true;
        js["message"] = "Login success";
        js["data"]["redirect"] = WWW_PREVIEW_REDIRECT;
    }
    else {
        js["success"] = false;
        js["message"] = "Invalid username or password";
    }
    js["timestamp"] = (uint32_t)time(NULL);
    HTTP_ResponseDataAsJSON(message, status, js.dump(), extraHeader);
}


static void APIV1_CGI_UserLogout(FCGX_Request& message) {
    nlohmann::json js;
    std::string cookie = std::string("Set-Cookie: ") + JWT_AUTHORISE_SESSION + "=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly; SameSite=Strict";
    js["success"] = true;
    js["message"] = "Logout success";
    js["data"]["redirect"] = WWW_LOGIN_REDIRECT;
    HTTP_ResponseDataAsJSON(message, 200, js.dump(), cookie);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_MediaLivestream(FCGX_Request& message) {
    nlohmann::json js;
    std::string filename = std::string(WWW_ROOT) + WWW_VIDEO_MP4;
    long long total = GetFileSize(filename);
    if (total <= 0) {
        js["success"] = false;
        js["message"] = "Livestream returns failure";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
        return;
    }
    long long start = 0;
    long long end = total - 1;
    int status = 200;
    std::string range = stGetEnvirVariables(message, "HTTP_RANGE");
    if (!range.empty()) {
        if (!HTTP_ExtractRangeHeader(range, total, start, end)) {
            HTTP_ResponseRangeNotSatisfiable(message, total);
            return;
        }
        status = 206;
    }
    if (end >= total) {
        end = total - 1;
    }

    std::string video;
    if (!stReadFileChunkBinary(filename, start, end - start + 1, video)) {
        js["success"] = false;
        js["message"] = "Can't read video data";
        HTTP_ResponseDataAsJSON(message, 500, js.dump());
        return;
    }

    HTTP_ResponseDataAsChunkBinaries(message, status, "video/mp4",video, start, start + (long long)video.size() - 1, total);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
HashTableEntrance POST_HashMap[] = {
    {(char*)"/api/v1/user/login"            , false , APIV1_CGI_UserLogin	    },
    {(char*)"/api/v1/user/logout"           , true  , APIV1_CGI_UserLogout	    },
    {(char*)"/api/v1/media/livestream"      , true  , APIV1_CGI_MediaLivestream	},

    // {(char*)"/api/v1/stream/answer"		, apiv1_AnswerStreamConnection	},
	// {(char*)"/api/v1/device/reboot"		, apiv1_SetDeviceReboot			},
	// {(char*)"/api/v1/device/reset"		, apiv1_SetDeviceReset			},
    // {(char*)"/api/v1/media/video"		, apiv1_SetMediaVideo			},
	// {(char*)"/api/v1/media/audio"		, apiv1_SetMediaAudio			},
	// {(char*)"/api/v1/media/image"		, apiv1_SetMediaImage			},
	// {(char*)"/api/v1/control/led"		, apiv1_SetControlLed			},
	// {(char*)"/api/v1/control/motors"	    , apiv1_SetControlMotors		},
	// {(char*)"/api/v1/control/siren"		, apiv1_SetControlSiren			},
	// {(char*)"/api/v1/events/detection"	, apiv1_SetEventsDetection		},
	// {(char*)"/api/v1/network/wireless"	, apiv1_SetNetworkWireless		},
    /* -------------------------------- EOF --------------------------------*/
    {NULL                                   , false , NULL                  },
};
