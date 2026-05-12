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

static void APIV1_CGI_UserLogin(FCGX_Request& message, nlohmann::json& js) {
    int status = 401;
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
        js["data"]["redirect"] = WWW_PREVIEW_REDIRECT;
    }
    else {
        js["success"] = false;
        js["message"] = "Invalid username or password";
    }
    js["timestamp"] = (uint32_t)time(NULL);
    HTTP_ResponseDataAsJSON(message, status, js.dump(), extraHeader);
}


static void APIV1_CGI_UserLogout(FCGX_Request& message, nlohmann::json& js) {
    std::string cookie = 
        std::string("Set-Cookie: ") + 
        JWT_AUTHORISE_SESSION + 
        "=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly; SameSite=Strict";
    js["data"]["redirect"] = WWW_LOGIN_REDIRECT;
    HTTP_ResponseDataAsJSON(message, 200, js.dump(), cookie);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_MediaLivestream(FCGX_Request& message, nlohmann::json& js) {
    std::string filename = std::string(WWW_ROOT) + WWW_VIDEO_MP4;
    long long total = GetFileSize(filename);
    if (total <= 0) {
        js["success"] = false;
        js["message"] = "Livestream return failure";
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
static void APIV1_CGI_MediaVideo(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_MediaAudio(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_MediaImage(FCGX_Request& message, nlohmann::json& js) {
    int rc = 0;
    std::string body = HTTP_ExtractBodyContent(message);
    {
        nlohmann::json _js = nlohmann::json::parse(body);
        
        /*
            {
            "isp": {
                "hue": 50,
                "wdr": 0,
                "contrast": 50,
                "sharpness": 50,
                "brightness": 50,
                "saturation": 50,
                "flip_mirror": 0,
                "anti_flicker": 50,
                "anti_fogging": 0
            },
            "smart_ir": {
                "switch_context": 0
            },
            "osd": {
                "font_size": 32,
                "font_color": "#ffffff",
                "datetime": {
                    "enabled": true
                },
                "watermark": {
                    "enabled": true,
                    "content": "C340-VIVOO-PROD",
                    "position_x": 16,
                    "position_y": 440
                },
                "image_logo": {
                    "enabled": true
                }
            },
            "privacy_masks": [
                {
                "enabled": false,
                "position_x": 0,
                "position_y": 0,
                "width": 0,
                "height": 0
                }
            ]
            }
        */
        /*
            @isp
        */
        if (_js.contains("isp")) {
            int hue = _js["isp"]["hue"].get<int>();
            int wdr = _js["isp"]["wdr"].get<int>();
            int contrast = _js["isp"]["contrast"].get<int>();
            int sharpness = _js["isp"]["sharpness"].get<int>();
            int brightness = _js["isp"]["brightness"].get<int>();
            int saturation = _js["isp"]["saturation"].get<int>();
            int flip_mirror = _js["isp"]["flip_mirror"].get<int>();
            int anti_flicker = _js["isp"]["anti_flicker"].get<int>();
            int anti_fogging = _js["isp"]["anti_fogging"].get<int>();
            if (flip_mirror == 1) {
                rc |= rk_isp_set_image_flip(0, "flip");
            } else if (flip_mirror == 2) {
                rc |= rk_isp_set_image_flip(0, "mirror");
            } else if (flip_mirror == 3) {
                rc |= rk_isp_set_image_flip(0, "centrosymmetric");
            } else {
                rc |= rk_isp_set_image_flip(0, "close");
            }
            if (anti_flicker == 50) {
                rc |= rk_isp_set_power_line_frequency_mode(0, "PAL(50HZ)");
            } else {
                rc |= rk_isp_set_power_line_frequency_mode(0, "NTSC(60HZ)");
            }
            rc |= rk_isp_set_hue(0, hue);
            rc |= rk_isp_set_contrast(0, contrast);
            rc |= rk_isp_set_sharpness(0, sharpness);
            rc |= rk_isp_set_brightness(0, brightness);
            rc |= rk_isp_set_saturation(0, saturation);
        }
        /*
            @smart_ir
        */
        if (_js.contains("smart_ir")) {
            char *value = NULL;
            if (0 == _js["smart_ir"]["switch_context"].get<int>()) {
                value = (char*)"day";
            } else if (1 == _js["smart_ir"]["switch_context"].get<int>()) {
                value = (char*)"night";
            } else {
                value = (char*)"auto";
            }
            rc |= rk_isp_get_night_to_day(0, &value);
        }
        /*
            @osd
        */
        if (_js.contains("osd")) {
            if (_js["osd"].contains("font_size") &&
                _js["osd"].contains("font_color")) {
                int fontSize = _js["osd"]["font_size"].get<int>();
                std::string fontColor = _js["osd"]["font_color"].get<std::string>();
                rc |= rk_osd_set_font_size(fontSize);
                rc |= rk_osd_set_font_color(fontColor.c_str());
            }
            if (_js["osd"].contains("datetime")) {
                bool enabled = _js["osd"]["datetime"]["enabled"].get<bool>();
                rc |= rk_osd_set_enabled(1, enabled);
            }
            if (_js["osd"].contains("watermark")) {
                bool enabled = _js["osd"]["watermark"]["enabled"].get<bool>();
                std::string content = _js["osd"]["watermark"]["content"].get<std::string>();
                int x = _js["osd"]["watermark"]["position_x"].get<int>();
                int y = _js["osd"]["watermark"]["position_y"].get<int>();
                rc |= rk_osd_set_enabled(2, enabled);
                rc |= rk_osd_set_position_x(2, x);
                rc |= rk_osd_set_position_y(2, y);
                rc |= rk_osd_set_display_text(2, content.c_str());
            }
            if (_js["osd"].contains("image_logo")) {
                bool enabled = _js["osd"]["image_logo"]["enabled"].get<bool>();
                rc |= rk_osd_set_enabled(6, enabled);
            }
        }
        /*
            @privacy_masks
        */
        if (_js.contains("privacy_masks")) {
            int id = 0;
            for (auto it : _js["privacy_masks"]) {
                int chnlId = (id == 0) ? 4 : 5;
                int x = it["position_x"].get<int>();
                int y = it["position_y"].get<int>();
                int w = it["width"].get<int>();
                int h = it["height"].get<int>();
                bool enabled = it["enabled"].get<bool>();
                rc |= rk_osd_set_enabled(chnlId, enabled);
                rc |= rk_osd_set_position_x(chnlId, x);
                rc |= rk_osd_set_position_y(chnlId, y);
                rc |= rk_osd_set_width(chnlId, w);
                rc |= rk_osd_set_height(chnlId, h);
                ++(id);
            }
        }
    }
    if (rc != 0) {
        js["success"] = false;
        js["message"] = "Set image return failure";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
        return;
    }
    else {
        HTTP_ResponseDataAsJSON(message, 200, js.dump());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_SystemReboot(FCGX_Request& message, nlohmann::json& js) {
    // TODO: reboot after 1 seconds
}

static void APIV1_CGI_SystemReset(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_SystemUpgrade(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_SystemTime(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_SystemInformation(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_SystemUsersAdd(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_SystemUsersUpdate(FCGX_Request& message, nlohmann::json& js) {
    
}


static void APIV1_CGI_SystemUsersDelete(FCGX_Request& message, nlohmann::json& js) {
    
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_GpioLed(FCGX_Request& message, nlohmann::json& js) {
    std::string body = HTTP_ExtractBodyContent(message);
    {
        nlohmann::json _js = nlohmann::json::parse(body);
        bool value = _js["enabled"].get<bool>();
        // TODO
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_GpioSiren(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_GpioLighting(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_GpioMotors(FCGX_Request& message, nlohmann::json& js) {
    
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_EventAlarm(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_EventDetection(FCGX_Request& message, nlohmann::json& js) {
    
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_IndustrialRS485(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_IndustriaQRBarcode(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_IndustriaEndpoint(FCGX_Request& message, nlohmann::json& js) {
    
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
HashTableEntrance POST_HashMap[] = {
    /*
        @User
    */
    {(char*)"/api/v1/user/login"                , false , APIV1_CGI_UserLogin	        },
    {(char*)"/api/v1/user/logout"               , true  , APIV1_CGI_UserLogout	        },
    /*
        @Stream
        @Playback
    */
    {(char*)"/api/v1/media/livestream"          , true  , APIV1_CGI_MediaLivestream	    },
    /*
        @Media
    */
    {(char*)"/api/v1/media/video"               , true  , APIV1_CGI_MediaVideo	        },
    {(char*)"/api/v1/media/audio"               , true  , APIV1_CGI_MediaAudio	        },
    {(char*)"/api/v1/media/image"               , true  , APIV1_CGI_MediaImage	        },
    /*
        @Network
    */
    {(char*)"/api/v1/network/wifi/connect"      , true  , APIV1_CGI_MediaVideo	        },
    {(char*)"/api/v1/network/wifi/disconnect"   , true  , APIV1_CGI_MediaAudio	        },
    {(char*)"/api/v1/network/lan/mode"          , true  , APIV1_CGI_MediaImage	        },
    /*
        @System
    */
    {(char*)"/api/v1/system/reboot"             , true  , APIV1_CGI_SystemReboot        },
    {(char*)"/api/v1/system/reset"              , true  , APIV1_CGI_SystemReset	        },
    {(char*)"/api/v1/system/upgrade"            , true  , APIV1_CGI_SystemUpgrade	    },
    {(char*)"/api/v1/system/time"               , true  , APIV1_CGI_SystemTime	        },
    {(char*)"/api/v1/system/information"        , true  , APIV1_CGI_SystemInformation   },
    {(char*)"/api/v1/system/users/add"          , true  , APIV1_CGI_SystemUsersAdd	    },
    {(char*)"/api/v1/system/users/update"       , true  , APIV1_CGI_SystemUsersUpdate   },
    {(char*)"/api/v1/system/users/delete"       , true  , APIV1_CGI_SystemUsersDelete   },
    /*
        @GPIO
    */
    {(char*)"/api/v1/gpio/led"                  , true  , APIV1_CGI_GpioLed	            },
    {(char*)"/api/v1/gpio/siren"                , true  , APIV1_CGI_GpioSiren	        },
    {(char*)"/api/v1/gpio/lighting"             , true  , APIV1_CGI_GpioLighting	    },
    {(char*)"/api/v1/gpio/motors"               , true  , APIV1_CGI_GpioMotors	        },
    /*
        @Event
    */
    {(char*)"/api/v1/event/alarm"               , true  , APIV1_CGI_EventAlarm	        },
    {(char*)"/api/v1/event/detection"           , true  , APIV1_CGI_EventDetection	    },
    /*
        @Industrial IO
    */
    {(char*)"/api/v1/industrial/rs485"          , true  , APIV1_CGI_IndustrialRS485     },
    {(char*)"/api/v1/industrial/qr_barcode"     , true  , APIV1_CGI_IndustriaQRBarcode  },
    {(char*)"/api/v1/industrial/endpoint"       , true  , APIV1_CGI_IndustriaEndpoint   },  
    /*
        @End of function
    */
    {(char*)NULL                                 , false , (CGI_FunCallback)NULL         },
};
