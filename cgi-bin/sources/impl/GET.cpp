#include "main.h"
#include "http_utils.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_RedirectLoginPage(FCGX_Request& message) {
    std::string html = stReadFile(std::string(WWW_ROOT) + WWW_INDEX_PAGE);
    if (html.empty()) {
        nlohmann::json js;
        js["success"] = false;
        js["message"] = "Page not found";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    }
    else {
        HTTP_ResponseDataAsHTML(message, 200, html);
    }
}

static void APIV1_CGI_RedirectPreviewPage(FCGX_Request& message) {
    if (!HTTP_IsAuthenticated(message)) {
        HTTP_ResponseRedirect(message, WWW_LOGIN_REDIRECT);
        return;
    }
    std::string html = stReadFile(std::string(WWW_ROOT) + WWW_PREVIEW_PAGE);
    if (html.empty()) {
        nlohmann::json js;
        js["success"] = false;
        js["message"] = "Page not found";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    }
    else {
        HTTP_ResponseDataAsHTML(message, 200, html);
    }
}

static void APIV1_CGI_MediaSnapshot(FCGX_Request& message) {
    nlohmann::json js;
    std::string image = stReadBinaryFile(std::string(WWW_ROOT) + WWW_IMAGE_JPEG);
    if (image.empty()) {
        js["success"] = false;
        js["message"] = "Snapshot returns failure";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    }
    else {
        js["success"] = true;
        js["message"] = "Snapshot returns success";
        HTTP_ResponseDataAsBinaries(message, 200, js.dump(), image);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
HashTableEntrance GET_HashMap[] = {
    {(char*)"/login"                        , false , APIV1_CGI_RedirectLoginPage	},
    {(char*)"/preview"                      , false , APIV1_CGI_RedirectPreviewPage	},
    {(char*)"/api/v1/media/snapshot"        , true  , APIV1_CGI_MediaSnapshot	    },
    {NULL                                   , false , NULL                          },
    
    // {(char*)"/api/v1/stream/answer"		, apiv1_AnswerStreamConnection	},
	// {(char*)"/api/v1/device/reboot"		, apiv1_SetDeviceReboot			},
	// {(char*)"/api/v1/device/reset"		, apiv1_SetDeviceReset			},
    // {(char*)"/api/v1/media/video"		, apiv1_SetMediaVideo			},
	// {(char*)"/api/v1/media/audio"		, apiv1_SetMediaAudio			},
	// {(char*)"/api/v1/media/image"		, apiv1_SetMediaImage			},
	// {(char*)"/api/v1/control/led"		, apiv1_SetControlLed			},
	// {(char*)"/api/v1/control/motors"	, apiv1_SetControlMotors		    },
	// {(char*)"/api/v1/control/siren"		, apiv1_SetControlSiren			},
	// {(char*)"/api/v1/events/detection"	, apiv1_SetEventsDetection		},
	// {(char*)"/api/v1/network/wireless"	, apiv1_SetNetworkWireless		},
};
