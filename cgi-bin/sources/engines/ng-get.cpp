#include "main.h"
#include "helpers.h"
#include <sys/statvfs.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_RedirectLoginPage(FCGX_Request &message, nlohmann::json &js) {
    std::string html = readFile(std::string(WWW_ROOT) + WWW_INDEX_PAGE);

    if (html.empty()) {
        js["success"] = false;
        js["message"] = "Page not found";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    } else {
        HTTP_ResponseDataAsHTML(message, 200, html);
    }
}

static void APIV1_CGI_RedirectPreviewPage(FCGX_Request &message, nlohmann::json &js) {
    if (!HTTP_IsAuthenticated(message)) {
        HTTP_ResponseRedirect(message, WWW_REDIRECT_LOGIN);
        return;
    }
    std::string html = readFile(std::string(WWW_ROOT) + WWW_INDEX_PAGE);
    if (html.empty()) {
        js["success"] = false;
        js["message"] = "Page not found";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    } else {
        HTTP_ResponseDataAsHTML(message, 200, html);
    }
}

static void APIV1_CGI_GetActivateStatus(FCGX_Request &message, nlohmann::json &js) {
    extern bool *REGISTRY_REQUIRED_AUTHEN;
    /**
     * If REGISTRY_REQUIRED_AUTHEN = TRUE mean activate MUST be TRUE 
     * for first time registry on web UI
     */
	js["data"]["activate"] = (*REGISTRY_REQUIRED_AUTHEN) ? false : true;
	HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_MediaSnapshot(FCGX_Request &message, nlohmann::json &js) {
#define TMP_IMAGE_SNAPSHOT RAM_ROOT "/image.jpeg"

    uint8_t counts = 0;
    do {
        rk_take_photo(TMP_IMAGE_SNAPSHOT);
        sleep(1);
        if (access(TMP_IMAGE_SNAPSHOT, F_OK) == 0) {
            break;
        }
        ++(counts);
    } while (counts < 3);

    std::string data = readBin(TMP_IMAGE_SNAPSHOT);
    if (data.empty()) {
        js["success"] = false;
        js["message"] = "Snapshot return failure";
        HTTP_ResponseDataAsJSON(message, 404, js.dump());
    } else {
        HTTP_ResponseDataAsBinaries(message, 200, js.dump(), data);
    }
    unlink(TMP_IMAGE_SNAPSHOT);
}

static void APIV1_CGI_MediaVideo(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];

    for (uint8_t n = 0; n < 2; ++n) {
        char *chnlNamed = (n == 0) ? (char *)"mainStream" : (char *)"minorStream";
        data[chnlNamed]["width"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":width").c_str(), -1);
        data[chnlNamed]["height"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":height").c_str(), -1);
        data[chnlNamed]["fps"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":src_frame_rate_num").c_str(), -1);
        data[chnlNamed]["gop"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":gop").c_str(), -1);
        data[chnlNamed]["minBitrate"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":mid_rate").c_str(), -1);
        data[chnlNamed]["maxBitrate"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":max_rate").c_str(), -1);
        data[chnlNamed]["rcMode"] = rk_param_get_string((std::string("video.") + std::to_string(n) + ":rc_mode").c_str(), NULL);
        data[chnlNamed]["minQp"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":frame_min_qp").c_str(), -1);
        data[chnlNamed]["maxQp"] = rk_param_get_int((std::string("video.") + std::to_string(n) + ":frame_max_qp").c_str(), -1);
        std::string quality = rk_param_get_string((std::string("video.") + std::to_string(n) + ":rc_quality").c_str(), "high");
        if (quality == "lowest") {
            data[chnlNamed]["rcQuality"] = 1;
        } else if (quality == "lower") {
            data[chnlNamed]["rcQuality"] = 2;
        } else if (quality == "low") {
            data[chnlNamed]["rcQuality"] = 3;
        } else if (quality == "high") {
            data[chnlNamed]["rcQuality"] = 4;
        } else if (quality == "higher") {
            data[chnlNamed]["rcQuality"] = 5;
        } else if (quality == "highest") {
            data[chnlNamed]["rcQuality"] = 6;
        }
        const char *codec = rk_param_get_string((std::string("video.") + std::to_string(n) + ":output_data_type").c_str(), "H.264");
        if (strcmp(codec, "H.265") == 0) {
            data[chnlNamed]["encodeType"] = "H265";
        }  else {
            data[chnlNamed]["encodeType"] = "H264";
        }
    }

    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_MediaAudio(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];
    data["microphone"]["volume"] = rk_param_get_int("audio.0:volume", 100);
    data["microphone"]["gain"] = rk_param_get_int("audio.0:gain", 0);
    data["microphone"]["echoCancellation"] = rk_param_get_int("audio.0:enable_aed", 0) != 0;
    data["microphone"]["noiseSuppression"] = rk_param_get_int("audio.0:enable_vqe", 0) != 0;
    data["speaker"]["volume"] = rk_param_get_int("audio.0:volume", 100);
    data["speaker"]["softwareAmplifier"] = rk_param_get_int("audio.0:software_amplifier", 0) != 0;

    const char *sav_str = rk_param_get_string("audio.0:software_amplifier_value", "1.0");
    double sav = sav_str ? std::atof(sav_str) : 1.0;
    data["speaker"]["softwareAmplifierValue"] = sav;

    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_MediaImage(FCGX_Request &message, nlohmann::json &js) {
    char *value = NULL;
    nlohmann::json &data = js["data"];

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
        data["isp"]["flipMirror"] = 1;
    } else if (strcmp(value, "mirror") == 0) {
        data["isp"]["flipMirror"] = 2;
    } else if (strcmp(value, "centrosymmetric") == 0) {
        data["isp"]["flipMirror"] = 3;
    } else {
        data["isp"]["flipMirror"] = 0;
    }
    value = rk_param_get_string("isp.0.video_adjustment:power_line_frequency_mode", "PAL(50HZ)");
    data["isp"]["antiFlicker"] = (strcmp(value, "PAL(50HZ)") == 0) ? 50 : 60;
    value = rk_param_get_string("isp.0.enhancement:dehaze", "close");
    data["isp"]["antiFogging"] = (strcmp(value, "close") != 0) ? true : false;
    /*
        @isp.white_balance
    */
    value = rk_param_get_string("isp.0.white_blance:white_blance_style", "autoWhiteBalance");
    data["isp"]["whiteBalance"]["mode"] = (strcmp(value, "manualWhiteBalance") == 0) ? 1 : 0;
    data["isp"]["whiteBalance"]["red"] = rk_param_get_int("isp.0.white_blance:white_blance_red", 50);
    data["isp"]["whiteBalance"]["green"] = rk_param_get_int("isp.0.white_blance:white_blance_green", 50);
    data["isp"]["whiteBalance"]["blue"] = rk_param_get_int("isp.0.white_blance:white_blance_blue", 50);
    /*
        @isp.exposure
    */
    value = rk_param_get_string("isp.0.exposure:exposure_mode", "auto");
    data["isp"]["exposure"]["mode"] = (strcmp(value, "manual") == 0) ? 1 : 0;
    data["isp"]["exposure"]["time"] = rk_param_get_string("isp.0.exposure:exposure_time", "1/25");
    data["isp"]["exposure"]["gainMode"] = rk_param_get_string("isp.0.exposure:gain_mode", "auto");
    data["isp"]["exposure"]["gain"] = rk_param_get_int("isp.0.exposure:exposure_gain", 1);
    /*
        @isp.noise_reduction
    */
    value = rk_param_get_string("isp.0.enhancement:noise_reduce_mode", "close");
    data["isp"]["noiseReduction"]["mode"] = value;
    if (strcmp(value, "2dnr") == 0) {
        data["isp"]["noiseReduction"]["level"] = rk_param_get_int("isp.0.enhancement:spatial_denoise_level", 50);
    } else {
        data["isp"]["noiseReduction"]["level"] = rk_param_get_int("isp.0.enhancement:temporal_denoise_level", 50);
    }
    /*
        @smart_ir
    */
    data["smartIr"]["irDimmer"] = rk_param_get_int("isp.1.night_to_day:light_brightness", 50);
    data["smartIr"]["switchContext"] = rk_param_get_int("isp:auto_day_night", 1);
    /*
        @osd
    */
    data["osd"]["fontSize"] = rk_param_get_int("osd.common:font_size", -1);
    data["osd"]["fontColor"] = rk_param_get_string("osd.common:font_color", NULL);
    data["osd"]["datetime"]["enabled"] = (rk_param_get_int("osd.1:enabled", -1) == 1);
    data["osd"]["datetime"]["positionX"] = rk_param_get_int("osd.1:position_x", -1);
    data["osd"]["datetime"]["positionY"] = rk_param_get_int("osd.1:position_y", -1);
    data["osd"]["watermark"]["enabled"] = (rk_param_get_int("osd.0:enabled", -1) == 1);
    data["osd"]["watermark"]["content"] = rk_param_get_string("osd.0:display_text", NULL);
    data["osd"]["watermark"]["positionX"] = rk_param_get_int("osd.0:position_x", -1);
    data["osd"]["watermark"]["positionY"] = rk_param_get_int("osd.0:position_y", -1);
    data["osd"]["imageLogo"]["enabled"] = (rk_param_get_int("osd.6:enabled", -1) == 1);
    data["osd"]["imageLogo"]["positionX"] = rk_param_get_int("osd.6:position_x", -1);
    data["osd"]["imageLogo"]["positionY"] = rk_param_get_int("osd.6:position_y", -1);
    data["osd"]["modbus"]["enabled"] = (rk_param_get_int("osd.3:enabled", -1) == 1);
    data["osd"]["modbus"]["positionX"] = rk_param_get_int("osd.3:position_x", -1);
    data["osd"]["modbus"]["positionY"] = rk_param_get_int("osd.3:position_y", -1);
    data["osd"]["rs485"]["enabled"] = (rk_param_get_int("osd.3:enabled", -1) == 1);
    data["osd"]["rs485"]["positionX"] = rk_param_get_int("osd.3:position_x", -1);
    data["osd"]["rs485"]["positionY"] = rk_param_get_int("osd.3:position_y", -1);
    /*
        @privacy_masks
    */
    int norm_w = rk_param_get_int("osd.common:normalized_screen_width", 704);
    int norm_h = rk_param_get_int("osd.common:normalized_screen_height", 480);
    if (norm_w <= 0)
        norm_w = 704;
    if (norm_h <= 0)
        norm_h = 480;
    int x4 = rk_param_get_int("osd.4:position_x", -1);
    int y4 = rk_param_get_int("osd.4:position_y", -1);
    int w4 = rk_param_get_int("osd.4:width", -1);
    int h4 = rk_param_get_int("osd.4:height", -1);

    if (x4 != -1)
        x4 = (x4 * 2304) / norm_w;
    if (y4 != -1)
        y4 = (y4 * 1296) / norm_h;
    if (w4 != -1)
        w4 = (w4 * 2304) / norm_w;
    if (h4 != -1)
        h4 = (h4 * 1296) / norm_h;

    int x5 = rk_param_get_int("osd.5:position_x", -1);
    int y5 = rk_param_get_int("osd.5:position_y", -1);
    int w5 = rk_param_get_int("osd.5:width", -1);
    int h5 = rk_param_get_int("osd.5:height", -1);

    if (x5 != -1)
        x5 = (x5 * 2304) / norm_w;
    if (y5 != -1)
        y5 = (y5 * 1296) / norm_h;
    if (w5 != -1)
        w5 = (w5 * 2304) / norm_w;
    if (h5 != -1)
        h5 = (h5 * 1296) / norm_h;

    data["privacyMasks"].push_back(
        nlohmann::json({{"enabled", (rk_param_get_int("osd.4:enabled", -1) == 1)},
                        {"width", w4},
                        {"height", h4},
                        {"positionX", x4},
                        {"positionY", y4}}));
    data["privacyMasks"].push_back(
        nlohmann::json({{"enabled", (rk_param_get_int("osd.5:enabled", -1) == 1)},
                        {"width", w5},
                        {"height", h5},
                        {"positionX", x5},
                        {"positionY", y5}}));

    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_NetworkStream(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];
    {
        if (access(APP_RTMP_CONFIGURE_FILE, F_OK) == 0) {
            std::string content = readFile(APP_RTMP_CONFIGURE_FILE);
            data = nlohmann::json::parse(content);
        }
        else {
            /* Assign peasudo values */
            data["enabled"] = false;
            data["channel"] = 1;
            data["endpoint"] = "rtmps://customer-server.com:1443/live";
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_NetworkStatus(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];

    /* INTERFACE: WLAN0 */
    {
        bool isConnected = false;
        const char *ifname = (const char *)"wlan0";
        char ssid[32] = {0}, ip4[16] = {0}, gw4[16] = {0}, subnet4[16] = {0}, dns[16] = {0};

        if (Kiwi_NIC_GetStatus(ifname) == KIWI_IF_UP) {
            isConnected = Kiwi_WiFi_IsConnected(ifname);
            Kiwi_DNS_GetAddress(NULL, dns);
            Kiwi_IP4_GetAddress(ifname, NULL, ip4);
            Kiwi_WiFi_GetSSIDConnected(ifname, (char *)ssid, sizeof(ssid));
            Kiwi_GATEWAY_GetAddress(ifname, NULL, gw4, NULL, subnet4);
        }
        data["wifi"]["connected"] = isConnected;
        data["wifi"]["ssid"] = std::string(ssid);
        data["wifi"]["ipAddress"] = std::string(ip4);
        data["wifi"]["gateway"] = std::string(gw4);
        data["wifi"]["subnetMask"] = std::string(subnet4);
        data["wifi"]["dns"].push_back(std::string(dns));
    }
    /* INTERFACE: ETH0 */
    {
        bool dhcp = true;
        std::string configure {};
        KIWI_NETWORK_NIC_STATUS status;
        const char *ifname = (const char *)"eth0";
        char ip4[16] = {0}, mac[18] = {0}, gw4[16] = {0}, subnet4[16] = {0}, dns[16] = {0};

        const std::string WIRED_RUNTIME_CONFIG = "/tmp/wired.conf";
        if (access(WIRED_RUNTIME_CONFIG.c_str(), F_OK) == 0) {
            configure = readFile(WIRED_RUNTIME_CONFIG);
            /* Example: "static 192.168.2.111 255.255.255.0 192.168.2.253 192.168.2.253" */
            std::istringstream ss(configure);
            std::string mode;
            ss >> mode;
            if (mode == "static") {
                dhcp = false;
                std::string ip;
                std::string mask;
                std::string dns1;
                std::string gateway;
                ss >> ip >> mask >> gateway >> dns1;
                snprintf(ip4, sizeof(ip4), "%s", ip.c_str());
                snprintf(subnet4, sizeof(subnet4), "%s", mask.c_str());
                snprintf(gw4, sizeof(gw4), "%s", gateway.c_str());
                snprintf(dns, sizeof(dns), "%s", dns1.c_str());
            }
        }

        if (dhcp) {
            status = Kiwi_NIC_GetStatus(ifname);
            if (status != KIWI_IF_UNKNOWN) {
                Kiwi_DNS_GetAddress(NULL, dns);
                Kiwi_IP4_GetAddress(ifname, NULL, ip4);
                Kiwi_GATEWAY_GetAddress(ifname, NULL, gw4, NULL, subnet4);
            }
        }
        Kiwi_MAC_GetAddress(ifname, NULL, mac);
        
        data["lan"]["dhcp"] = dhcp;
        data["lan"]["connected"] = (status == KIWI_IF_UP) ? true : false;
        data["lan"]["ipAddress"] = std::string(ip4);
        data["lan"]["macAddress"] = std::string(mac);
        data["lan"]["gateway"] = std::string(gw4);
        data["lan"]["subnetMask"] = std::string(subnet4);
        data["lan"]["dns"].push_back(std::string(dns));
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_NetworkProtocols(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];
    {
        if (access(APP_PROTOCOLS_CONFIGURE_FILE, F_OK) == 0) {
            std::string content = readFile(APP_PROTOCOLS_CONFIGURE_FILE);
            data = nlohmann::json::parse(content);
        }
        else {
            /* Assign pseudo values */
            data["http"]["port"] = 443;
            data["rtsp"]["port"] = 554;
            data["rtsp"]["tls"] = false;
            data["rtsp"]["enabled"] = true;
            data["onvif"]["enabled"] = true;
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_NetworkWiFiScan(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];

    KIWI_NETWORK_WIFI_SCAN_S list[32] = {0};
    if (Kiwi_WiFi_Scan() == 0) {
        int count = Kiwi_WiFi_GetList(list, 32);
        for (int id = 0; id < count; id++) {
            if (strlen(list[id].ssid) > 0) {
                data["list"].push_back(
                    nlohmann::json({
                        {"ssid"     , list[id].ssid     },
                        {"bssid"    , list[id].bssid    },
                        {"strength" , list[id].strength },
                        {"security" , list[id].security }
                    })
                );
            }
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_SystemTime(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];
    {
        if (access(APP_NTPD_CONFIGURE_FILE, F_OK) == 0) {
            std::string content = readFile(APP_NTPD_CONFIGURE_FILE);
            data = nlohmann::json::parse(content);
        }
        else {
            /* Assign pseudo values */
            data["utcOffset"] = std::string("UTC+07:00");
            data["timezone"] = std::string("Asia/Ho_Chi_Minh");
            data["ntp"]["enabled"] = false;
            data["ntp"]["servers"].push_back(std::string("0.pool.ntp.org"));
            data["ntp"]["servers"].push_back(std::string("1.pool.ntp.org"));
            data["ntp"]["servers"].push_back(std::string("2.pool.ntp.org"));
            data["ntp"]["servers"].push_back(std::string("3.pool.ntp.org"));
            data["ntp"]["servers"].push_back(std::string("time.google.com"));
        }
        char dateStr[32] = {0};
        char timeStr[32] = {0};
        time_t ts = time(NULL);
        struct tm *local = localtime(&ts);
        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", local);
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", local);
        data["date"] = std::string(dateStr);
        data["time"] = std::string(timeStr);
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemInformation(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];
    {
        std::string stVersion = readFile(APP_VERSION_FILE);
        nlohmann::json _js = nlohmann::json::parse(stVersion); 
        data["model"] = _js["Title"].get<std::string>();
        data["manufacturer"] = rk_param_get_string("system.device_info:manufacturer", "UNKOWN");
        data["serialNumber"] = readFile(APP_UNIQUE_SERIAL_FILE);
        data["releaseDate"] = _js["SoftwareBuildTime"].get<std::string>();
        data["firmwareVersion"] = _js["Version"].get<std::string>();
        data["releaseDatetime"] = _js["SoftwareBuildTime"].get<std::string>();
        data["hardwareVersion"] = rk_param_get_string("system.device_info:hardware_version", "UNKOWN");
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemUsersList(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];

    KIWI_CREDENTIALS_T list[32] = {0};
    int size = Kiwi_Credentials_Get(list, sizeof(list) / sizeof(KIWI_CREDENTIALS_T));
    for (int id = 0; id < size; id++) {
        nlohmann::json _js;
        _js["role"] = list[id].role;
        _js["username"] = list[id].username;
        data["list"].push_back(_js);
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_StorageMode(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];
    {
        std::string mode = readFile(APP_STORAGE_CONFIGURE_FILE);
        if (mode.empty()) {
            data = nlohmann::json::object();
        } else {
            data = nlohmann::json::parse(mode);
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_StorageStatus(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];
    {
        const char *mountpoint = (const char*)"/mnt/sdcard";
        bool mounted = runCommands("mountpoint -q %s", mountpoint) == 0 ? true : false;
        if (!mounted) {
            data["free"] = 0;
            data["used"] = 0;
            data["capacity"] = 0;
        } else {
            struct statvfs fs = {0};
            statvfs(mountpoint, &fs);
            data["capacity"] = (uint64_t)fs.f_blocks * fs.f_frsize;
            data["free"] = (uint64_t)fs.f_bavail * fs.f_frsize;
            data["used"] = data["capacity"].get<uint64_t>() - data["free"].get<uint64_t>();
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_GpioStatus(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];
    {
        std::string spotLightValue = readFile("/sys/class/pwm/pwmchip7/pwm0/duty_cycle");
        data["sirenStatus"] = false;
        data["spotlightStatus"] = (spotLightValue == "10000") ? true : false;
        data["motorsPosition"]["horizone"] = -1;
        data["motorsPosition"]["vertical"] = -1;
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_GpioLighting(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];
    {
        data["mode"] = rk_param_get_int("spotlight:mode", 0);
        data["dimmer"] = rk_param_get_int("spotlight:dimmer", 0);
        const std::string schedule = std::string(rk_param_get_string("spotlight:schedule", ""));
        if (schedule.empty()) {
            data["schedule"] = nlohmann::json::object();
        } else {
            data["schedule"] = nlohmann::json::parse(schedule);
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_EventAlarm(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];
    {
        if (access(APP_EVENT_DISPATCHER_FILE, F_OK) == 0) {
            std::string content = readFile(APP_EVENT_DISPATCHER_FILE);
            data = nlohmann::json::parse(content);
        }
        else {
            /* Assign peasudo values */
            data["enabled"] = false;
            data["alarm"]["intervalMs"] = 15000;
            data["alarm"]["upload"]["protocol"] = "https";
            data["alarm"]["upload"]["endpoint"] = "https://customer-server.com/api/device-data";
            data["alarm"]["upload"]["port"] = 443;
            data["alarm"]["upload"]["tls"]["enabled"] = false;
            data["alarm"]["upload"]["tls"]["verifyPeer"] = false;
            data["alarm"]["upload"]["tls"]["caCertificate"] = "";
            data["alarm"]["action"]["sound"] = false;
            data["alarm"]["action"]["lighting"] = false;
            const std::vector<std::string> days = {
                "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"
            };
            for (const auto& day : days) {
                data["schedule"][day]["enabled"] = false;
                data["schedule"][day]["slots"] = nlohmann::json::array();
            }
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_EventDetection(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];
    {
        data["enabled"] = rk_param_get_int("video.source:enable_object_detect", 1) ? true : false;
        data["objectDetection"]["type"] = rk_param_get_int("video.source:object_detect_filter", 1);
        data["objectDetection"]["boundingBox"] = rk_param_get_int("video.source:enable_object_shape", 0) ? true : false;
        data["objectDetection"]["sensitivityLevel"] = rk_param_get_int("ivs:md_sensibility", 2);
        data["regionOfInterest"]["rule"] = rk_param_get_int("roi.3:id", 0);
        data["regionOfInterest"]["shape"] = rk_param_get_string("roi.3:name", "polygon");
        std::string points = rk_param_get_string("roi.3:points", "");
        if (points.empty()) {
            data["regionOfInterest"]["points"] = nlohmann::json::array();
        }
        else data["regionOfInterest"]["points"] = nlohmann::json::parse(points);
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_IndustrialRS485(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json& data = js["data"];
    {
        if (access(APP_INDUS_RS485_FILE, F_OK) == 0) {
            std::string content = readFile(APP_INDUS_RS485_FILE);
            data = nlohmann::json::parse(content);
        }
        else {
            /* Assign peasudo values */
            data["enabled"] = false;
            data["configuration"]["type"] = "rtu";
            data["configuration"]["baudrate"] = 115200;
            data["configuration"]["databits"] = 8;
            data["configuration"]["stopbits"] = 1;
            data["configuration"]["parity"] = "N";
            data["configuration"]["hardwareFlowControl"] = false;
            data["configuration"]["pollingIntervalMs"] = 15000;
            data["devices"].push_back(nlohmann::json::object());
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_IndustriaQRBarcode(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];
    {
        if (access(APP_INDUS_QRCODE_FILE, F_OK) == 0) {
            std::string content = readFile(APP_INDUS_QRCODE_FILE);
            data = nlohmann::json::parse(content);
        }
        else {
            /* Assign peasudo values */
            data["enabled"] = false;
            data["configuration"]["deviceType"] = 1;
            data["configuration"]["height"] = 0;
            data["configuration"]["width"] = 0;
            data["configuration"]["positionX"] = 0;
            data["configuration"]["positionY"] = 0;
            data["configuration"]["channel"] = 0;
            data["configuration"]["pollingIntervalMs"] = 15000;
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_IndustriaEndpoint(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];
    {
        if (access(APP_INDUS_ENDPOINT_FILE, F_OK) == 0) {
            std::string content = readFile(APP_INDUS_ENDPOINT_FILE);
            data = nlohmann::json::parse(content);
        }
        else {
            /* Assign peasudo values */
            data["enabled"] = false;
            data["protocol"] = "https";
            data["endpoint"] = "https://customer-server.com/api/device-data";
            data["port"] = 443;
            data["reportPeriodicMs"] = 15000;
            data["tls"]["enabled"] = false;
            data["tls"]["verifyPeer"] = false;
            data["tls"]["caCertificate"] = "";
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
HashTableEntrance GET_HashMap[] = {
    /*
        @Browser
    */
    {(char *)"/login"                       , false ,   Customer    , APIV1_CGI_RedirectLoginPage       },
    {(char *)"/preview"                     , false ,   Customer    , APIV1_CGI_RedirectPreviewPage     },
	{(char *)"/activate"                    , false ,   Customer    , APIV1_CGI_GetActivateStatus       },
    /*
        @Media
    */
    {(char *)"/api/v1/media/snapshot"       , true  ,   Customer    , APIV1_CGI_MediaSnapshot           },
    {(char *)"/api/v1/media/video"          , true  ,   Customer    , APIV1_CGI_MediaVideo              },
    {(char *)"/api/v1/media/audio"          , true  ,   Customer    , APIV1_CGI_MediaAudio              },
    {(char *)"/api/v1/media/image"          , true  ,   Customer    , APIV1_CGI_MediaImage              },
    /*
        @Network
    */
    {(char *)"/api/v1/network/stream"       , true  ,   Customer     , APIV1_CGI_NetworkStream          },
    {(char *)"/api/v1/network/status"       , true  ,   Customer     , APIV1_CGI_NetworkStatus          },
    {(char *)"/api/v1/network/protocols"    , true  ,   Customer     , APIV1_CGI_NetworkProtocols       },
    {(char *)"/api/v1/network/wifi/scan"    , true  ,   Customer     , APIV1_CGI_NetworkWiFiScan        },
    /*
        @System
    */
    {(char *)"/api/v1/system/time"          , true  ,   Customer     , APIV1_CGI_SystemTime             },
    {(char *)"/api/v1/system/information"   , true  ,   Customer     , APIV1_CGI_SystemInformation      },
    {(char *)"/api/v1/system/users/list"    , true  ,   Customer     , APIV1_CGI_SystemUsersList        },
    /*
        @Storage
    */
    {(char *)"/api/v1/storage/mode"         , true  ,   Customer     , APIV1_CGI_StorageMode            },
    {(char *)"/api/v1/storage/status"       , true  ,   Customer     , APIV1_CGI_StorageStatus          },
    /*
        @GPIO
    */
    {(char *)"/api/v1/gpio/status"          , true  ,   Customer     , APIV1_CGI_GpioStatus             },
    {(char *)"/api/v1/gpio/lighting"        , true  ,   Customer     , APIV1_CGI_GpioLighting           },
    /*
        @Event
    */
    {(char *)"/api/v1/event/alarm"          , true  ,   Customer     , APIV1_CGI_EventAlarm             },
    {(char *)"/api/v1/event/detection"      , true  ,   Customer     , APIV1_CGI_EventDetection         },
    /*
        @Industrial IO
    */
    {(char *)"/api/v1/industrial/rs485"     , true  ,   Customer     , APIV1_CGI_IndustrialRS485        },
    {(char *)"/api/v1/industrial/qr_barcode", true  ,   Customer     , APIV1_CGI_IndustriaQRBarcode     },
    {(char *)"/api/v1/industrial/endpoint"  , true  ,   Customer     , APIV1_CGI_IndustriaEndpoint      },
    /*
        @End of function
    */
    {(char *)NULL                           , false ,   Customer     , (CGI_FunCallback)NULL            }
};
