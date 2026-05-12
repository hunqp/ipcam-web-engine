#include "main.h"
#include "http_utils.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_RedirectLoginPage(FCGX_Request& message, nlohmann::json& js) {
    std::string html = stReadFile(std::string(WWW_ROOT) + WWW_INDEX_PAGE);

    CGI_SYSD("HTML: %s\r\n", html.c_str());

    if (html.empty()) {
        js["success"] = false;
        js["message"] = "Page not found";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    }
    else {
        HTTP_ResponseDataAsHTML(message, 200, html);
    }
}

static void APIV1_CGI_RedirectPreviewPage(FCGX_Request& message, nlohmann::json& js) {
    if (!HTTP_IsAuthenticated(message)) {
        HTTP_ResponseRedirect(message, WWW_LOGIN_REDIRECT);
        return;
    }
    std::string html = stReadFile(std::string(WWW_ROOT) + WWW_PREVIEW_PAGE);
    if (html.empty()) {
        js["success"] = false;
        js["message"] = "Page not found";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    }
    else {
        HTTP_ResponseDataAsHTML(message, 200, html);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_MediaSnapshot(FCGX_Request& message, nlohmann::json& js) {
    int rc = -1;
    uint8_t counts = 0;
    const char *filename = (const char*)"/tmp/snapshot.jpg";

	do {
        rc = rk_take_photo(filename);
        if (rc == 0) break;
		++(counts);
		sleep(1);
	}
    while (counts < 3);

    std::string data = stReadBinaryFile(filename);
    if (data.empty()) {
        js["success"] = false;
        js["message"] = "Snapshot return failure";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    }
    else {
        HTTP_ResponseDataAsBinaries(message, 200, js.dump(), data);
    }
}

static void APIV1_CGI_MediaVideo(FCGX_Request& message, nlohmann::json& js) {
    nlohmann::json data = js["data"];

    for (uint8_t n = 0; n < 2; ++n) {
        char *chnlNamed = (n == 0) ? (char*)"main_stream" : (char*)"minor_stream";
        data[chnlNamed]["width"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":width").c_str(), -1);
        data[chnlNamed]["height"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":height").c_str(), -1);
        data[chnlNamed]["fps"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":src_frame_rate_num").c_str(), -1);
        data[chnlNamed]["gop"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":gop").c_str(), -1);
        data[chnlNamed]["min_bitrate"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":mid_rate").c_str(), -1);
        data[chnlNamed]["max_bitrate"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":max_rate").c_str(), -1);
        data[chnlNamed]["rc_mode"] = rk_param_get_string((std::string("video.") + std::to_string(n) + ":rc_mode").c_str(), NULL);
        data[chnlNamed]["rc_quality"] = rk_param_get_string((std::string("video.") + std::to_string(n) + ":rc_quality").c_str(), NULL);
        data[chnlNamed]["encode_type"] = rk_param_get_string((std::string("video.") + std::to_string(n) + ":output_data_type").c_str(), NULL);
        data[chnlNamed]["min_qp"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":frame_min_qp").c_str(), -1);
        data[chnlNamed]["max_qp"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":frame_max_qp").c_str(), -1);
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_MediaAudio(FCGX_Request& message, nlohmann::json& js) {
    nlohmann::json data = js["data"];
    data["microphone"]["volume"] = rk_param_get_int("audio.volume", -1);
    data["microphone"]["gain"] = rk_param_get_int("audio.gain", -1);
    data["microphone"]["echo_cancellation"] = rk_param_get_int("audio.enable_aed", false);
    data["microphone"]["noise_suppression"] = rk_param_get_int("audio.enable_vqe", false);
    data["speaker"]["volume"] = rk_param_get_int("audio.volume", -1);
    data["speaker"]["software_amplifier"] = rk_param_get_int("audio.software_amplifier", -1);
    data["speaker"]["software_amplifier_value"] = rk_param_get_int("audio.software_amplifier_value", -1);
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_MediaImage(FCGX_Request& message, nlohmann::json& js) {
    char *value = NULL;
    nlohmann::json data = js["data"];

    /*
        @isp
    */
    data["isp"]["wdr"] = rk_param_get_int("isp.0.blc:wdr_level", -1);
    data["isp"]["hue"] = rk_param_get_int("isp.0.adjustment:hue", -1);
    data["isp"]["contrast"] = rk_param_get_int("isp.0.adjustment:contrast", -1);
    data["isp"]["sharpness"] = rk_param_get_int("isp.0.adjustment:sharpness", -1);
    data["isp"]["brightness"] = rk_param_get_int("isp.0.adjustment:brightness", -1);
    data["isp"]["saturation"] = rk_param_get_int("isp.0.adjustment:saturation", -1);
    value = rk_param_get_string("isp.0.video_adjustment:image_flip", NULL);
	if (strcmp(value, "flip") == 0) {
		data["isp"]["flip_mirror"] = 1;
	} else if (strcmp(value, "mirror") == 0) {
		data["isp"]["flip_mirror"] = 2;
	} else if (strcmp(value, "centrosymmetric") == 0) {
		data["isp"]["flip_mirror"] = 3;
	} else {
        data["isp"]["flip_mirror"] = 0;
    }
    value = rk_param_get_string("isp.0.video_adjustment:power_line_frequency_mode", NULL);
    if (strcmp(value, "PAL(50HZ)") == 0) {
        data["isp"]["anti_flicker"] = 50;
    } else {
        data["isp"]["anti_flicker"] = 60;
    }
    data["isp"]["anti_fogging"] = rk_param_get_int("isp.0.enhancement:dehaze_level", -1);
    /*
        @smart_ir
    */
    data["smart_ir"]["switch_context"] = rk_param_get_int("isp:auto_day_night", -1);
    /*
        @osd
    */
    data["osd"]["font_size"] = rk_param_get_int("osd.common:font_size", -1);
    data["osd"]["font_color"] = rk_param_get_string("osd.common:font_color", NULL);
    data["osd"]["datetime"]["enabled"] = rk_param_get_int("osd.1:enabled", -1);
    data["osd"]["watermark"]["enabled"] = rk_param_get_int("osd.0:enabled", -1);
    data["osd"]["watermark"]["content"] = rk_param_get_string("osd.0:display_text", NULL);
    data["osd"]["watermark"]["position_x"] = rk_param_get_int("osd.0:position_x", -1);
    data["osd"]["watermark"]["position_y"] = rk_param_get_int("osd.0:position_y", -1);
    data["osd"]["image_logo"]["enabled"] = rk_param_get_int("osd.6:enabled", -1);
    /*
        @privacy_masks
    */
    data["privacy_masks"].push_back(nlohmann::json({
        {"enabled", rk_param_get_int("osd.4:enabled", -1)},
        {"width", rk_param_get_int("osd.4:width", -1)},
        {"height", rk_param_get_int("osd.4:height", -1)},
        {"position_x", rk_param_get_int("osd.4:position_x", -1)},
        {"position_y", rk_param_get_int("osd.4:position_y", -1)}
    }));
    data["privacy_masks"].push_back(nlohmann::json({
        {"enabled", rk_param_get_int("osd.5:enabled", -1)},
        {"width", rk_param_get_int("osd.5:width", -1)},
        {"height", rk_param_get_int("osd.5:height", -1)},
        {"position_x", rk_param_get_int("osd.5:position_x", -1)},
        {"position_y", rk_param_get_int("osd.5:position_y", -1)}
    }));

    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_NetworkStatus(FCGX_Request& message, nlohmann::json& js) {
    nlohmann::json data = js["data"];

    /*
        @wifi
    */
    {    
        bool isConnected = false;
        const char *ifname = (const char*)"wlan0";
        char ssid[32] = {0}, ip4[16] = {0}, gw4[16] = {0}, subnet4[16] = {0}, dns[16];

        if (Kiwi_NIC_GetStatus(ifname) == KIWI_IF_UP) {
            isConnected = Kiwi_WiFi_IsConnected(ifname);
            Kiwi_DNS_GetAddress(NULL, dns);
            Kiwi_IP4_GetAddress(ifname, NULL, ip4);
            Kiwi_WiFi_GetSSIDConnected(ifname, (char*)ssid, sizeof(ssid));
            Kiwi_GATEWAY_GetAddress(ifname, NULL, gw4, NULL, subnet4);   
        }
        data["wifi"]["connected"] = isConnected;
        data["wifi"]["ssid"] = std::string(ssid);
        data["wifi"]["ip_address"] = std::string(ip4);
        data["wifi"]["gateway"] = std::string(gw4);
        data["wifi"]["subnet_mask"] = std::string(subnet4);
        data["wifi"]["dns"].push_back(std::string(dns));
    }
    /*
        @lan
    */
    {
        bool isConnected = false, isDhcp = false;
        const char *ifname = (const char*)"eth0";
        char ip4[16] = {0}, mac[18] = {0}, gw4[16] = {0}, subnet4[16] = {0}, dns[16];
        
        if (Kiwi_NIC_GetStatus(ifname) == KIWI_IF_UP) {
            isDhcp = rk_param_get_int("network.static:enable", -1) == 1 ? false : true;
            isConnected = Kiwi_WiFi_IsConnected(ifname);
            Kiwi_DNS_GetAddress(NULL, dns);
            Kiwi_IP4_GetAddress(ifname, NULL, ip4);
            Kiwi_MAC_GetAddress(ifname, NULL, mac);
            Kiwi_GATEWAY_GetAddress(ifname, NULL, gw4, NULL, subnet4);
        }
        data["lan"]["dhcp"] = isDhcp;
        data["lan"]["connected"] = isConnected;
        data["lan"]["ip_address"] = std::string(ip4);
        data["lan"]["mac_address"] = std::string(mac);
        data["lan"]["gateway"] = std::string(gw4);
        data["lan"]["subnet_mask"] = std::string(subnet4);
        data["lan"]["dns"].push_back(std::string(dns));
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_NetworkProtocols(FCGX_Request& message, nlohmann::json& js) {
    nlohmann::json data = js["data"];
    data["http"] = 80;
    data["rtsp"] = 554;
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_NetworkWiFiScan(FCGX_Request& message, nlohmann::json& js) {
    nlohmann::json data = js["data"];
    
    KIWI_NETWORK_WIFI_SCAN_S list[32] = {0};
    if (Kiwi_WiFi_Scan() == 0) {
        int count = Kiwi_WiFi_GetList(list, 32);
        for (int id = 0; id < count; id++) {
            if (strlen(list[id].ssid) > 0) {
                data["list"].push_back(nlohmann::json({
                    {"ssid"     , list[id].ssid      },
                    {"bssid"    , list[id].bssid     },
                    {"strength" , list[id].strength  },
                    {"security" , list[id].security  }
                }));
            }
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_SystemTime(FCGX_Request& message, nlohmann::json& js) {
    
}


static void APIV1_CGI_SystemInformation(FCGX_Request& message, nlohmann::json& js) {
    
}

static void APIV1_CGI_SystemUsersList(FCGX_Request& message, nlohmann::json& js) {
    
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_GpioStatus(FCGX_Request& message, nlohmann::json& js) {
    
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
HashTableEntrance GET_HashMap[] = {
    /*
        @Browser
    */
    {(char*)"/login"                        , false , APIV1_CGI_RedirectLoginPage	},
    {(char*)"/preview"                      , false , APIV1_CGI_RedirectPreviewPage	},
    /*
        @Media
    */
    {(char*)"/api/v1/media/snapshot"        , true  , APIV1_CGI_MediaSnapshot	    },
    {(char*)"/api/v1/media/video"           , true  , APIV1_CGI_MediaVideo	        },
    {(char*)"/api/v1/media/audio"           , true  , APIV1_CGI_MediaAudio	        },
    {(char*)"/api/v1/media/image"           , true  , APIV1_CGI_MediaImage	        },
    /*
        @Network
    */
    {(char*)"/api/v1/network/status"        , true  , APIV1_CGI_NetworkStatus	    },
    {(char*)"/api/v1/network/protocols"     , true  , APIV1_CGI_NetworkProtocols	},
    {(char*)"/api/v1/network/wifi/scan"     , true  , APIV1_CGI_NetworkWiFiScan	    },
    /*
        @System
    */
    {(char*)"/api/v1/system/time"           , true  , APIV1_CGI_SystemTime	        },
    {(char*)"/api/v1/system/information"    , true  , APIV1_CGI_SystemInformation	},
    {(char*)"/api/v1/system/users/list"     , true  , APIV1_CGI_SystemUsersList	    },
    /*
        @GPIO
    */
    {(char*)"/api/v1/gpio/status"           , true  , APIV1_CGI_GpioStatus  	    },

    /*
        @Event
    */
    {(char*)"/api/v1/event/alarm"           , true  , APIV1_CGI_EventAlarm          },
    {(char*)"/api/v1/event/detection"       , true  , APIV1_CGI_EventDetection      },
    /*
        @Industrial IO
    */
    {(char*)"/api/v1/industrial/rs485"      , true  , APIV1_CGI_IndustrialRS485     },
    {(char*)"/api/v1/industrial/qr_barcode" , true  , APIV1_CGI_IndustriaQRBarcode  },
    {(char*)"/api/v1/industrial/endpoint"   , true  , APIV1_CGI_IndustriaEndpoint   },
    /*
        @End of function
    */
    {(char*)NULL                            , false , (CGI_FunCallback)NULL         }
};
