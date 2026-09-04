/* System libraries */
#include <cerrno>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

/* User inlcude */
#include "main.h"
#include "utils.h"
#include "network.h"
#include "http_utils.h"
#include "self-signed.h"
#include "attemps_login.h"
#include "dispatchtimer.h"

/**
 * This variable used for limiting user login attempts
 * Preventing BRUT FORCE attacks
 */
static struct {
    bool isLocked = false;
    uint32_t u32LockedTime = 0;
    uint32_t u32LastAccess = 0;
    uint16_t u16FailedAttempts = 0;
    uint16_t u16GeometricSequence = 0;
    /* Default constant values */
    const uint16_t U16_MAX_LOGIN_ATTEMPTS  = 15U;
    const uint32_t U32_MAX_LOCKOUT_SECONDS = 300U; /* 5 minutes */

    void reset(bool bResetByTimeout) {
        isLocked = false;
        u32LockedTime = 0;
        u32LastAccess = 0;
        u16FailedAttempts = 0;
        if (!bResetByTimeout) {
            u16GeometricSequence = 0;
        }
    }
} sAttemptUserLogin;

static DispatchTimer sMainTimer;
static std::string sUpgradeSecret;
static std::string sUpgradeMD5Sum;
static size_t sUpgradeTotalSize;
static size_t sUpgradeBytesReceived;
static const char *FW_UPGRADE_DIR = RAM_ROOT "/ipc_firmware";
static const char *FW_UPGRADE_PACKAGED = RAM_ROOT "/ipc_firmware/fw_package.bin";
static const char *FW_UPGRADE_FILENAME = RAM_ROOT "/ipc_firmware/p2p_client.tar.gz";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern std::string stGetEnvirVariables(FCGX_Request &request, const char *name);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool isStrongPassword(const std::string &password) {
    /* Minimum length check (e.g., 8 characters) */
    if (password.length() < 8) {
        return false;
    }

    bool hasUpper = false, hasLower = false;
    bool hasDigit = false, hasSpecial = true /* Ignore this case */;

    for (uint8_t id = 0; id < password.length(); ++id) {
        if (isupper(password[id])) hasUpper = true;
        else if (islower(password[id])) hasLower = true;
        else if (isdigit(password[id])) hasDigit = true;
        // else if (strchr("!@#$%^&*()_+-=[]{}|;:,.<>?", password[id])) {
        //     hasSpecial = true;
        // }
    }
    /* If any required category is missing, consider it weak */
    if (!hasUpper || !hasLower || !hasDigit || !hasSpecial) {
        return false;
    }
    return true;
}

static bool validateCredentials(const std::string &username, const std::string &password, int *usrLevels) {
    KIWI_CREDENTIALS_T list[32] = {0};

    int size = Kiwi_Credentials_Get(list, 32);
    for (int id = 0; id < size; id++) {
        CGI_SYSI("%s:%s:%d\r\n", list[id].username, list[id].password, list[id].role);
        if (strcmp(list[id].username, username.c_str()) == 0 &&
            strcmp(list[id].password, password.c_str()) == 0) {
            *usrLevels = list[id].role;
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_UserLogin(FCGX_Request &message, nlohmann::json &js) {
    int status = 401;
    char username[32] = {0};
    char password[32] = {0};
    std::string extraHeader;
    eUserLevels role = Administrator;
    std::string body = HTTP_ExtractBodyContent(message);

    /* LOCKED ! Break at this segment code */
    uint32_t u32Ts = (uint32_t)time(NULL);
    if (sAttemptUserLogin.isLocked) {
        if (u32Ts > sAttemptUserLogin.u32LockedTime) {
            /* Expired and UNLOCK */
            sAttemptUserLogin.reset(true);
        } else {
            sAttemptUserLogin.u32LastAccess = u32Ts;
            js["success"] = false;
            js["message"] = "Too many failed login attempts! Please try again later " 
                            + std::to_string(sAttemptUserLogin.u32LockedTime - sAttemptUserLogin.u32LastAccess) 
                            + " seconds.";
            HTTP_ResponseDataAsJSON(message, 401, js.dump());
            return;
        }
    }

    /* %127[^&] means read up to 127 characters */
    if (sscanf(body.c_str(), "username=%127[^&]&password=%127s", username, password) == 2) {
        HTTP_DecodeSubmitForm(username, username);
        HTTP_DecodeSubmitForm(password, password);
        if (validateCredentials(username, password, (int*)&role)) {
            status = 200;
            extraHeader = HTTP_GenerateCookies(username, (int)role);
        }
    }

    if (status == 200) {
        js["data"]["role"] = role;
        js["data"]["redirect"] = WWW_REDIRECT_PREVIEW;
        js["data"]["username"] = std::string(username);
        sAttemptUserLogin.reset(false);
    } else {
        if ((++sAttemptUserLogin.u16FailedAttempts) >= sAttemptUserLogin.U16_MAX_LOGIN_ATTEMPTS) {
            sAttemptUserLogin.isLocked = true;
            sAttemptUserLogin.u16GeometricSequence++;
            uint32_t u32Timeout = sAttemptUserLogin.u16GeometricSequence * sAttemptUserLogin.U32_MAX_LOCKOUT_SECONDS;
            sAttemptUserLogin.u32LockedTime = (uint32_t)time(NULL) + u32Timeout;
        }
        js["success"] = false;
        js["message"] = "Invalid username or password! You still have " 
            + std::to_string(sAttemptUserLogin.U16_MAX_LOGIN_ATTEMPTS - sAttemptUserLogin.u16FailedAttempts) 
            + " attempt(s).";
    }
    HTTP_ResponseDataAsJSON(message, status, js.dump(), extraHeader);
}

static void APIV1_CGI_UserLogout(FCGX_Request &message, nlohmann::json &js) {
    std::string cookie = std::string("Set-Cookie: ") + JWT_AUTHORISE_SESSION +
                         "=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 "
                         "00:00:00 GMT; HttpOnly; SameSite=Strict";
    js["data"]["redirect"] = WWW_REDIRECT_LOGIN;
    HTTP_ResponseDataAsJSON(message, 200, js.dump(), cookie);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_MediaVideo(FCGX_Request &message, nlohmann::json &js) {
    int rc = 0;
    int status = 200;
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        /*
            @main_stream
        */
        if (_js.contains("main_stream")) {
            nlohmann::json sjs = _js["main_stream"];
            
            int fps = sjs["fps"].get<int>();
            int gop = sjs["gop"].get<int>();
            int min_bitrate = sjs["min_bitrate"].get<int>();
            int max_bitrate = sjs["max_bitrate"].get<int>();
            std::string rc_mode = sjs["rc_mode"].get<std::string>();
            int rc_quality = sjs["rc_quality"].get<int>();
            std::string encode_type = sjs["encode_type"].get<std::string>();
            int min_qp = sjs["min_qp"].get<int>();
            int max_qp = sjs["max_qp"].get<int>();
            char stFps[8] = {0};
            snprintf(stFps, sizeof(stFps), "%d", fps);
            char resolution[16] = {0};
            snprintf(resolution, sizeof(resolution), "%d*%d", sjs["width"].get<int>(), sjs["height"].get<int>());
            const char *quality = "high";
            switch (rc_quality) {
            case 1: quality = "lowest";
            break;
            case 2: quality = "lower";
            break;
            case 3: quality = "low";
            break;
            case 4: quality = "high";
            break;
            case 5: quality = "higher";
            break;
            case 6: quality = "highest";
            break;
            default: quality = "high";
            break;
            }
            rc |= rk_video_set_resolution(0, resolution);
            rc |= rk_video_set_frame_rate_in(0, stFps);
            rc |= rk_video_set_frame_rate(0, stFps);
            rc |= rk_video_set_gop(0, gop);
            rc |= rk_video_set_max_rate(0, max_bitrate);
            rc |= rk_video_set_RC_mode(0, rc_mode.c_str());
            rc |= rk_video_set_rc_quality(0, quality);
            if (encode_type == "H265" || encode_type == "H.265") {
                rc |= rk_video_set_output_data_type(0, "H.265");
            } else {
                rc |= rk_video_set_output_data_type(0, "H.264");
                rc |= rk_video_set_h264_profile(0, "high");
            }
            rc |= rk_video_set_stream_type(0, "mainStream");
            rc |= rk_video_set_gop_mode(0, "normalP");
            rc |= rk_video_set_smart(0, "close");
        }
        /*
            @minor_stream
        */
        if (_js.contains("minor_stream")) {
            nlohmann::json sjs = _js["minor_stream"];
            int fps = sjs["fps"].get<int>();
            int gop = sjs["gop"].get<int>();
            int max_bitrate = sjs["max_bitrate"].get<int>();
            std::string rc_mode = sjs["rc_mode"].get<std::string>();
            int rc_quality = sjs["rc_quality"].get<int>();
            std::string encode_type = sjs["encode_type"].get<std::string>();

            char stFps[8] = {0};
            snprintf(stFps, sizeof(stFps), "%d", fps);
            char resolution[16] = {0};
            snprintf(resolution, sizeof(resolution), "%d*%d", sjs["width"].get<int>(), sjs["height"].get<int>());

            const char *quality = "high";
            switch (rc_quality) {
            case 1: quality = "lowest";  break;
            case 2: quality = "lower";   break;
            case 3: quality = "low";     break;
            case 4: quality = "high";    break;
            case 5: quality = "higher";  break;
            case 6: quality = "highest"; break;
            }

            rc |= rk_video_set_resolution(1, resolution);
            rc |= rk_video_set_frame_rate_in(1, stFps);
            rc |= rk_video_set_frame_rate(1, stFps);
            rc |= rk_video_set_gop(1, gop);
            rc |= rk_video_set_max_rate(1, max_bitrate);
            rc |= rk_video_set_RC_mode(1, rc_mode.c_str());
            rc |= rk_video_set_rc_quality(1, quality);
            if (encode_type == "H265" || encode_type == "H.265") {
                rc |= rk_video_set_output_data_type(1, "H.265");
            } else {
                rc |= rk_video_set_output_data_type(1, "H.264");
                rc |= rk_video_set_h264_profile(1, "high");
            }
            rc |= rk_video_set_stream_type(1, "minorStream");
            rc |= rk_video_set_gop_mode(1, "normalP");
            rc |= rk_video_set_smart(1, "close");
        }
    }
    if (rc != 0) {
        status = 400;
        js["success"] = false;
        js["message"] = "Operation failure";
    }
    HTTP_ResponseDataAsJSON(message, status, js.dump());
}

static void APIV1_CGI_MediaAudio(FCGX_Request &message, nlohmann::json &js) {
    int rc = 0;
    int status = 200;
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);

    /* MIC */
    {
        int gain = _js["microphone"]["gain"].get<int>();
        int volume = _js["microphone"]["volume"].get<int>();
        int echo_cancellation = _js["microphone"]["echo_cancellation"].get<bool>();
        int noise_suppression = _js["microphone"]["noise_suppression"].get<bool>();
        rc = rk_audio_set_volume(0, volume);
    }
    #if 0
    /* 
        SPEAKER 
        This model does not supported
    */
    {
        int volume = _js["speaker"]["volume"].get<int>();
        int software_amplifier = _js["speaker"]["software_amplifier"].get<bool>();
        int software_amplifier_value = _js["speaker"]["software_amplifier_value"].get<float>();
    }
    #endif
    
    if (rc != 0) {
        status = 400;
        js["success"] = false;
        js["message"] = "Operation failure";
    }
    HTTP_ResponseDataAsJSON(message, status, js.dump());
}

static void APIV1_CGI_MediaImage(FCGX_Request &message, nlohmann::json &js) {
    int rc = 0;
    int status = 200;
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
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
            if (anti_fogging > 0) {
                rc |= rk_isp_set_dehaze(0, "open");
                rc |= rk_isp_set_dehaze_level(0, anti_fogging);
            } else {
                rc |= rk_isp_set_dehaze(0, "close");
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
                value = (char *)"day";
            } else if (1 == _js["smart_ir"]["switch_context"].get<int>()) {
                value = (char *)"night";
            } else {
                value = (char *)"auto";
            }
            rc |= rk_isp_set_night_to_day(0, value);
        }
        /*
            @osd
        */
        if (_js.contains("osd")) {
            if (_js["osd"].contains("font_size") && _js["osd"].contains("font_color")) {
                int size = _js["osd"]["font_size"].get<int>();
                std::string color = _js["osd"]["font_color"].get<std::string>();
                rc |= rk_osd_set_font_size(size);
                rc |= rk_osd_set_font_color(color.c_str());
            }
            if (_js["osd"].contains("datetime")) {
                int x = _js["osd"]["datetime"]["position_x"].get<int>();
                int y = _js["osd"]["datetime"]["position_y"].get<int>();
                bool enabled = _js["osd"]["datetime"]["enabled"].get<bool>();
                rc |= rk_osd_set_enabled(1, enabled);
                rc |= rk_osd_set_position_x(1, x);
                rc |= rk_osd_set_position_y(1, y);
            }
            if (_js["osd"].contains("watermark")) {
                int x = _js["osd"]["watermark"]["position_x"].get<int>();
                int y = _js["osd"]["watermark"]["position_y"].get<int>();
                bool enabled = _js["osd"]["watermark"]["enabled"].get<bool>();
                std::string content = _js["osd"]["watermark"]["content"].get<std::string>();
                rc |= rk_osd_set_enabled(0, enabled);
                rc |= rk_osd_set_position_x(0, x);
                rc |= rk_osd_set_position_y(0, y);
                rc |= rk_osd_set_display_text(0, content.c_str());
            }
            if (_js["osd"].contains("image_logo")) {
                int x = _js["osd"]["image_logo"]["position_x"].get<int>();
                int y = _js["osd"]["image_logo"]["position_y"].get<int>();
                bool enabled = _js["osd"]["image_logo"]["enabled"].get<bool>();
                rc |= rk_osd_set_enabled(6, enabled);
                rc |= rk_osd_set_position_x(6, x);
                rc |= rk_osd_set_position_y(6, y);
            }
            if (_js["osd"].contains("rs485")) {
                int x = _js["osd"]["rs485"]["position_x"].get<int>();
                int y = _js["osd"]["rs485"]["position_y"].get<int>();
                bool enabled = _js["osd"]["rs485"]["enabled"].get<bool>();
                rc |= rk_osd_set_position_x(3, x);
                rc |= rk_osd_set_position_y(3, y);
                rc |= rk_osd_set_enabled(3, enabled);
            }
            if (rc == 0) {
                rk_osd_restart();
            }
        }
        /*
            @privacy_masks
            Privacy masks are scaled to the maximum stream resolution space
            (e.g. 1920x1080) because video_rtu.c draws them directly on the VI
            channel at max resolution.
        */
        if (_js.contains("privacy_masks")) {
            int norm_w = rk_param_get_int("osd.common:normalized_screen_width", 704);
            int norm_h = rk_param_get_int("osd.common:normalized_screen_height", 480);
            if (norm_w <= 0)
                norm_w = 704;
            if (norm_h <= 0)
                norm_h = 480;

            /* Always disable both masks first to handle deletion or empty array */
            if (_js["privacy_masks"].size() == 0) {
                rc |= rk_osd_set_enabled(4, false);
                rc |= rk_osd_set_enabled(5, false);
            } else {
                for (uint8_t id = 0; id < _js["privacy_masks"].size(); ++id) {
                    int selected = (id == 0) ? 4 : 5;
                    int w_web = _js["privacy_masks"][id]["width"].get<int>();
                    int h_web = _js["privacy_masks"][id]["height"].get<int>();
                    int x_web = _js["privacy_masks"][id]["position_x"].get<int>();
                    int y_web = _js["privacy_masks"][id]["position_y"].get<int>();
                    bool enabled = _js["privacy_masks"][id]["enabled"].get<bool>();
                    int x = (x_web * norm_w) / 2304;
                    int y = (y_web * norm_h) / 1296;
                    int w = (w_web * norm_w) / 2304;
                    int h = (h_web * norm_h) / 1296;

                    rc |= rk_osd_set_enabled(selected, enabled);
                    rc |= rk_osd_set_position_x(selected, x);
                    rc |= rk_osd_set_position_y(selected, y);
                    rc |= rk_osd_set_width(selected, w);
                    rc |= rk_osd_set_height(selected, h);
                }
            }
            if (rc == 0) {
                rk_osd_restart();
            }
        }
    }

    if (rc != 0) {
        status = 400;
        js["success"] = false;
        js["message"] = "Operation failure";
    }
    HTTP_ResponseDataAsJSON(message, status, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_NetworkStream(FCGX_Request &message, nlohmann::json &js) {
    if (access(APP_RTMP_BIN_SH, F_OK) != 0) {
        js["success"] = false;
        js["message"] = "Operation not supported";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    /**/
    int status = 200;
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        bool enabled = _js["enabled"].get<bool>();
        runCommands("/bin/sh %s close > /dev/null 2>&1", APP_RTMP_BIN_SH);
        if (enabled) {
            sMainTimer.dispatch("rtmp-server", 1500, []() {
                runCommands("/bin/sh %s start > /dev/null 2>&1", APP_RTMP_BIN_SH);
            });
        }
        wrteFile(APP_RTMP_CONFIGURE_FILE, _js.dump());
    }
    HTTP_ResponseDataAsJSON(message, status, js.dump());
}

static void APIV1_CGI_NetworkProtocols(FCGX_Request &message, nlohmann::json &js) {
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        wrteFile(APP_PROTOCOLS_CONFIGURE_FILE, _js.dump());
        sMainTimer.dispatch("protocols", 1000, []() {
            /* Restart RTSP server */
            runCommands("/oem/usr/etc/init.d/S51rtspd restart > /dev/null 2>&1");
            sleep(1);
            /* Restart ONVIF server */
            runCommands("/oem/usr/etc/init.d/S52wsdd restart > /dev/null 2>&1");
        });
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_NetworkWiFiConnect(FCGX_Request &message, nlohmann::json &js) {
    if (Kiwi_NIC_GetStatus("wlan0") == KIWI_IF_UNKNOWN) {
        js["success"] = false;
        js["message"] = "WiFi doesn't supported";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        sMainTimer.dispatch("network", 1500, [_js]() {
            std::string ssid = _js["ssid"].get<std::string>();
            std::string password = _js["password"].get<std::string>();
            Kiwi_WiFi_DoConnect(ssid.c_str(), password.c_str());
        });
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_NetworkWiFiDisconnect(FCGX_Request &message, nlohmann::json &js) {
    if (Kiwi_NIC_GetStatus("wlan0") == KIWI_IF_UNKNOWN) {
        js["success"] = false;
        js["message"] = "WiFi doesn't supported";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    Kiwi_WiFi_ForceClose();
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_NetworkLanMode(FCGX_Request &message, nlohmann::json &js) {
    if (Kiwi_NIC_GetStatus("eth0") == KIWI_IF_UNKNOWN) {
        js["success"] = false;
        js["message"] = "LAN doesn't supported";
        return;
    }
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        sMainTimer.dispatch("network", 1000, [_js]() {
            bool dhcp = _js["dhcp"].get<bool>();
            char *method = (dhcp) ? (char*)"dhcp" : (char*)"static";
            std::string ip_address = _js["ip_address"].get<std::string>();
            std::string subnet_mask = _js["subnet_mask"].get<std::string>();
            std::string gateway = _js["gateway"].get<std::string>();
            std::string dns0 = _js["dns"][0].get<std::string>();
            std::string dns1 = "";
            if (_js["dns"].size() > 1) {
                dns1 = _js["dns"][1].get<std::string>();
            }
            rk_network_ipv4_set("eth0", method, (char*)ip_address.c_str(), (char*)subnet_mask.c_str(), (char*)gateway.c_str());
            if (!dhcp) {
                rk_network_dns_set((char*)dns0.c_str(), dns1.length() > 0 ? (char*)dns1.c_str() : NULL);
            }
        });
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

////////////////////////////////////////////////////////////////////////S///////////////////////////////////////
static void APIV1_CGI_SystemReboot(FCGX_Request &message, nlohmann::json &js) {
    sMainTimer.dispatch("reboot-machine", 1500, []() {
        system("reboot");
    });

    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemReset(FCGX_Request &message, nlohmann::json &js) {
    sMainTimer.dispatch("reboot-machine", 1500, []() {    
        unlink(APP_JOURNAL_LOG_FILE);
        unlink(APP_ACCOUNTS_DB_FILE);
        unlink(APP_IPC_CONFIGURE_FILE);
        unlink(APP_WIFI_CONFIGURE_FILE);
        unlink(APP_REGISTERED_STATUS_FILE);
        runCommands("rm -f %s/*", APP_INTEGRATION_DIR);
        system("reboot");
    });

    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemUpgrade(FCGX_Request &message, nlohmann::json &js) {
    const std::string contentType = stGetEnvirVariables(message, "CONTENT_TYPE");

    /**
     * Parse request and get SECRET and MD5SUM for incoming package firmware
     */
    if (contentType.compare(0, 16, "application/json") == 0) {
        std::string body = HTTP_ExtractBodyContent(message);
        nlohmann::json _js = nlohmann::json::parse(body);
        std::string secret = _js["secret"].get<std::string>();
        std::string md5sum = _js["md5sum"].get<std::string>();
        bool validMd5 = (md5sum.length() == 32) ? true : false;
        for (char &let : md5sum) {
            validMd5 = validMd5 && std::isxdigit(static_cast<unsigned char>(let));
            let = static_cast<char>(std::tolower(static_cast<unsigned char>(let)));
        }
        if (secret.empty() || secret.length() > 256 || !validMd5) {
            js["success"] = false;
            js["message"] = "Invalid secret or md5sum";
            HTTP_ResponseDataAsJSON(message, 400, js.dump());
            return;
        }
        sUpgradeSecret = secret;
        sUpgradeMD5Sum = md5sum;
        sUpgradeBytesReceived = 0;
        sUpgradeTotalSize = 0;
        if (mkdir(FW_UPGRADE_DIR, 0755) != 0 && errno != EEXIST) {
            js["success"] = false;
            js["message"] = "Cannot create firmware directory";
            HTTP_ResponseDataAsJSON(message, 500, js.dump());
            return;
        }
        unlink(FW_UPGRADE_PACKAGED);
        js["message"] = "Ready to receive firmware";
        HTTP_ResponseDataAsJSON(message, 200, js.dump());
        return;
    }

    const std::string secret = stGetEnvirVariables(message, "HTTP_X_UPGRADE_SECRET");
    std::string md5sum = stGetEnvirVariables(message, "HTTP_X_FIRMWARE_MD5");
    for (char &let : md5sum) {
        let = static_cast<char>(std::tolower(static_cast<unsigned char>(let)));
    }
    if (sUpgradeSecret.empty() || (secret != sUpgradeSecret) || (md5sum != sUpgradeMD5Sum)) {
        js["success"] = false;
        js["message"] = "Firmware metadata does not match";
        HTTP_ResponseDataAsJSON(message, 409, js.dump());
        return;
    }

    const std::string contentRange = stGetEnvirVariables(message, "HTTP_CONTENT_RANGE");
    unsigned long long start = 0;
    unsigned long long end = 0;
    unsigned long long total = 0;
    if (sscanf(contentRange.c_str(), "bytes %llu-%llu/%llu", &start, &end, &total) != 3 ||
        end < start || total == 0 || end >= total) {
        js["success"] = false;
        js["message"] = "Invalid firmware Content-Range";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }

    /**
     * Download chunk by chunk of incoming packages firmware
     */
    size_t remaining = HTTP_ExtractBodyContentLength(message);
    const size_t chunkSize = static_cast<size_t>(end - start + 1);
    if (remaining != chunkSize || start != sUpgradeBytesReceived ||
        (sUpgradeTotalSize != 0 && total != sUpgradeTotalSize)) {
        js["success"] = false;
        js["message"] = "Firmware chunk offset or size does not match";
        HTTP_ResponseDataAsJSON(message, 409, js.dump());
        return;
    }
    if (sUpgradeTotalSize == 0) {
        sUpgradeTotalSize = static_cast<size_t>(total);
    }

    FILE *firmware = fopen(FW_UPGRADE_PACKAGED, start == 0 ? "wb" : "ab");
    bool success = firmware != NULL;
    size_t receivedSize = 0;
    char buffer[64 * 1024] = {0};
    while (success && remaining > 0) {
        int requested = (remaining > sizeof(buffer)) ? sizeof(buffer) : static_cast<int>(remaining);
        int received = FCGX_GetStr(buffer, requested, message.in);
        if (received <= 0 || fwrite(buffer, 1, received, firmware) != static_cast<size_t>(received)) {
            success = false;
            break;
        }
        receivedSize += static_cast<size_t>(received);
        remaining -= received;
    }
    if (firmware) {
        success = fclose(firmware) == 0 && success;
    }
    if (!success) {
        unlink(FW_UPGRADE_PACKAGED);
        sUpgradeSecret.clear();
        sUpgradeMD5Sum.clear();
        sUpgradeBytesReceived = 0;
        sUpgradeTotalSize = 0;
        js["success"] = false;
        js["message"] = "Firmware chunk upload failed";
        HTTP_ResponseDataAsJSON(message, 422, js.dump());
        return;
    }

    sUpgradeBytesReceived += receivedSize;
    if (sUpgradeBytesReceived < sUpgradeTotalSize) {
        js["message"] = "Firmware chunk uploaded";
        js["data"]["received_size"] = sUpgradeBytesReceived;
        js["data"]["total_size"] = sUpgradeTotalSize;
        HTTP_ResponseDataAsJSON(message, 200, js.dump());
        return;
    }

    const std::string md5 = MD5Sum(FW_UPGRADE_PACKAGED);
    if (md5 != md5sum) {
        js["success"] = false;
        js["message"] = "Firmware checksum mismatch";
        unlink(FW_UPGRADE_PACKAGED);
        sUpgradeSecret.clear();
        sUpgradeMD5Sum.clear();
        sUpgradeBytesReceived = 0;
        sUpgradeTotalSize = 0;
        HTTP_ResponseDataAsJSON(message, 422, js.dump());
        return;
    }

    /**
     * Validate SECRET and MD5SUM after download complete
     */
    char *stSigns = NULL;
    char *stError = strdup("UNKNOWN");
    if (validateSecret(sUpgradeSecret.c_str(), (char*)md5.c_str(), &stSigns, &stError)) {
        setenv("FW_DECRYPT_PASS", stSigns, 1);
        CGI_SYSD("%s\r\n", stSigns);
        int rc = runCommands("openssl enc -d -aes-256-cbc -pbkdf2 -iter 10000 -pass env:FW_DECRYPT_PASS  -in '%s' -out '%s'", FW_UPGRADE_PACKAGED, FW_UPGRADE_FILENAME);
        if (rc == 0) {
            unlink(FW_UPGRADE_PACKAGED);
            sMainTimer.dispatch("upgrade-machine", 1500, []() {
                runCommands(
                "cd %s && tar -xf %s && chmod +x install.sh && sh install.sh > /dev/null", 
                FW_UPGRADE_DIR,
                FW_UPGRADE_FILENAME);
            });
        } else {
            js["success"] = false;
            js["message"] = "Invalid package firmware";
            unlink(FW_UPGRADE_PACKAGED);
            sUpgradeSecret.clear();
            sUpgradeMD5Sum.clear();
            sUpgradeBytesReceived = 0;
            sUpgradeTotalSize = 0;
            free(stError);
            HTTP_ResponseDataAsJSON(message, 422, js.dump());
            return;
        }
    } else {
        js["success"] = false;
        js["message"] = std::string(stError);
        unlink(FW_UPGRADE_PACKAGED);
        sUpgradeSecret.clear();
        sUpgradeMD5Sum.clear();
        sUpgradeBytesReceived = 0;
        sUpgradeTotalSize = 0;
        free(stError);
        HTTP_ResponseDataAsJSON(message, 422, js.dump());
        return;
    }

    sUpgradeSecret.clear();
    sUpgradeMD5Sum.clear();
    sUpgradeBytesReceived = 0;
    sUpgradeTotalSize = 0;
    free(stError);
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemTime(FCGX_Request &message, nlohmann::json &js) {
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        std::string timezone = _js["timezone"].get<std::string>();
        wrteFile("/userdata/TZ", timezone);
        runCommands("ln -sf /oem/usr/share/zoneinfo/%s /etc/localtime", timezone.c_str());
        tzset();
        wrteFile(APP_NTPD_CONFIGURE_FILE, _js.dump());
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemInformation(FCGX_Request &message, nlohmann::json &js) {
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        std::string sn = _js["serial_number"].get<std::string>();
        wrteFile(APP_UNIQUE_SERIAL_FILE, sn);
        int rc = runCommands("sh generate-self-signed.sh %s && sync > /dev/null 2>&1", sn.c_str());
        if (rc != 0) {
            js["success"] = false;
            js["message"] = "Failed to register device serial number";
            HTTP_ResponseDataAsJSON(message, 422, js.dump());
            return;
        } else {
            js["success"] = true;
            js["message"] = "Completed. Device will be rebooted after one second.";
            sMainTimer.dispatch("reboot-machine", 1500, []() {
                system("reboot");
            });
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemUsersAdd(FCGX_Request &message, nlohmann::json &js) {
    int rc = 0;
    std::string body = HTTP_ExtractBodyContent(message);
    {
        nlohmann::json _js = nlohmann::json::parse(body);
        int role = _js["role"].get<int>();
        std::string username = _js["username"].get<std::string>();
        std::string password = _js["password"].get<std::string>();

        /**
         * Invalid password, the password MUST be contained at least 8 characters, 
         * at least 1 uppercase letter, at least 1 lowercase letter, at least 1 digit, 
         * at least 1 special character
         */
        if (!isStrongPassword(password)) {
            js["success"] = false;
            js["message"] = "Password is not strong enough. It must contain at least 8 characters, including uppercase, lowercase, digit.";
            HTTP_ResponseDataAsJSON(message, 422, js.dump());
            return;
        }

        KIWI_CREDENTIALS_T infor = {0};
        infor.role = role;
        snprintf(infor.username, sizeof(infor.username) - 1, "%s", username.c_str());
        snprintf(infor.password, sizeof(infor.password) - 1, "%s", password.c_str());
        rc = Kiwi_Credentials_Add(&infor);
    }
    if (rc != 0) {
        js["success"] = false;
        js["message"] = "Add user return failure";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemUsersUpdate(FCGX_Request &message, nlohmann::json &js) {
    int rc = 0;
    std::string body = HTTP_ExtractBodyContent(message);
    {
        nlohmann::json _js = nlohmann::json::parse(body);
        int role = _js["role"].get<int>();
        std::string username = _js["username"].get<std::string>();
        std::string password = _js["password"].get<std::string>();

        /**
         * Invalid password, the password MUST be contained at least 8 characters, 
         * at least 1 uppercase letter, at least 1 lowercase letter, at least 1 digit, 
         * at least 1 special character
         */
        if (!isStrongPassword(password)) {
            js["success"] = false;
            js["message"] = "Password is not strong enough. It must contain at least 8 characters, including uppercase, lowercase, digit.";
            HTTP_ResponseDataAsJSON(message, 422, js.dump());
            return;
        }
        
        KIWI_CREDENTIALS_T infor = {0};
        infor.role = role;
        snprintf(infor.username, sizeof(infor.username) - 1, "%s", username.c_str());
        snprintf(infor.password, sizeof(infor.password) - 1, "%s", password.c_str());
        rc = Kiwi_Credentials_Mod(&infor);
    }
    if (rc != 0) {
        js["success"] = false;
        js["message"] = "Update user return failure";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_SystemUsersDelete(FCGX_Request &message, nlohmann::json &js) {
    int rc = 0;
    std::string body = HTTP_ExtractBodyContent(message);
    {
        nlohmann::json _js = nlohmann::json::parse(body);
        std::string username = _js["username"].get<std::string>();
        
        KIWI_CREDENTIALS_T infor = {0};
        snprintf(infor.username, sizeof(infor.username) - 1, "%s", username.c_str());
        rc = Kiwi_Credentials_Del(&infor);
    }
    if (rc != 0) {
        js["success"] = false;
        js["message"] = "Delete user return failure";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_StorageMode(FCGX_Request &message, nlohmann::json &js) {
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        wrteFile(APP_STORAGE_CONFIGURE_FILE, _js.dump());
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_StorageFormat(FCGX_Request &message, nlohmann::json &js) {
    int rc = 0;
    int status = 200;
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        char hdd[32] = {0};
        char mountpoint[32] = {0};

        FILE *fp = fopen("/proc/mounts", "r");
        if (fp) {
            while (fscanf(fp, "%31s %31s %*s %*s %*d %*d", hdd, mountpoint) == 2) {
                if (strcmp(mountpoint, (const char*)"/mnt/sdcard") == 0) {
                    CGI_SYSD("Unmounting and formatting storage: %s -> %s\r\n", hdd, mountpoint);
                    rc = runCommands("killall -9 p2p_client && umount -l %s && mkfs.vfat %s > /dev/null 2>&1", mountpoint, hdd);
                    break;
                }
            }
            fclose(fp);
        }
    }
    if (rc != 0) {
        js["success"] = false;
        js["message"] = "Operation failure";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_GpioLed(FCGX_Request &message, nlohmann::json &js) {
    #define GPIO_LED_GREEN_PIN (32)
    #define GPIO_LED_AMBER_PIN (34)

    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        bool enabled = _js["enabled"].get<bool>();
        if (!enabled) {
            rk_gpio_set_value(GPIO_LED_GREEN_PIN, 0);
            rk_gpio_set_value(GPIO_LED_AMBER_PIN, 0);
        } else {
            rk_gpio_set_value(GPIO_LED_GREEN_PIN, 1);
        }
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_GpioSiren(FCGX_Request &message, nlohmann::json &js) {
    js["success"] = false;
    js["message"] = "Not Implemented";
    HTTP_ResponseDataAsJSON(message, 501, js.dump());
}

static void APIV1_CGI_GpioLighting(FCGX_Request &message, nlohmann::json &js) {
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        int mode = _js["mode"].get<int>();
        if (_js.contains("schedule")) {
            auto schedule = _js["schedule"];
            rk_gpio_set_spotlight_schedule((char*)schedule.dump().c_str());
        }
        rk_gpio_set_spotlight_mode(mode);
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_GpioMotors(FCGX_Request &message, nlohmann::json &js) {
    js["success"] = false;
    js["message"] = "Not Implemented";
    HTTP_ResponseDataAsJSON(message, 501, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_EventDetection(FCGX_Request &message, nlohmann::json &js) {
    int rc = 0;
    int status = 200;
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        /* 
            @Basic configurations
        */
        bool enabled = _js["enabled"].get<bool>();
        int type = _js["object_detection"]["type"].get<int>();
        bool bouding_box = _js["object_detection"]["bounding_box"].get<bool>();
        int sensitivity_level = _js["object_detection"]["sensitivity_level"].get<int>();
        rc |= rk_objdet_set_type(type);
        rc |= rk_objdet_set_enabled((int)enabled);
        rc |= rk_objdet_set_bounding_box((int)bouding_box);
        rc |= rk_objdet_set_sensitivity_levels(sensitivity_level);

        /* 
            @Advanced configurations
        */
        int rule = _js["region_of_interest"]["rule"].get<int>();
        std::string shape = _js["region_of_interest"]["shape"].get<std::string>();
        auto &points = _js["region_of_interest"]["points"];
        rc |= rk_roi_set_enabled(3, enabled);
        rc |= rk_roi_set_id(3, rule);
        rc |= rk_roi_set_name(3, shape.c_str());
        rc |= rk_roi_set_points(3, points.dump().c_str());
    }
    if (rc != 0) {
        status = 400;
        js["success"] = false;
        js["message"] = "Operation failure";
    }
    HTTP_ResponseDataAsJSON(message, status, js.dump());
}

static void APIV1_CGI_EventAlarm(FCGX_Request &message, nlohmann::json &js) {
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        /* 
            @We safe certificate in seperate for CURL using
        */
        std::string cert = _js["alarm"]["upload"]["tls"]["ca_certificate"].get<std::string>();
        _js["alarm"]["upload"]["tls"]["ca_certificate"].clear();
        wrteFile(APP_EVENT_DISPATCHER_CERT, cert);
        wrteFile(APP_EVENT_DISPATCHER_FILE, _js.dump());
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void APIV1_CGI_IndustrialRS485(FCGX_Request &message, nlohmann::json &js) {
    if (access(APP_MODBUS_BIN_SH, F_OK) != 0) {
        js["success"] = false;
        js["message"] = "Operation not supported";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    /**/
    int status = 200;
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        bool enabled = _js["enabled"].get<bool>();
        runCommands("/bin/sh %s close > /dev/null 2>&1", APP_MODBUS_BIN_SH);
        if (enabled) {
            sMainTimer.dispatch("rs485-server", 1500, []() {
                runCommands("/bin/sh %s start > /dev/null 2>&1", APP_MODBUS_BIN_SH);
            });
        }
        wrteFile(APP_INDUS_RS485_FILE, _js.dump());
    }
    HTTP_ResponseDataAsJSON(message, status, js.dump());
}

static void APIV1_CGI_IndustriaQRBarcode(FCGX_Request &message, nlohmann::json &js) {
    if (access(APP_QRCODE_BIN_SH, F_OK) != 0) {
        js["success"] = false;
        js["message"] = "Operation not supported";
        HTTP_ResponseDataAsJSON(message, 400, js.dump());
        return;
    }
    /**/
    int status = 200;
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        bool enabled = _js["enabled"].get<bool>();
        runCommands("/bin/sh %s close > /dev/null 2>&1", APP_QRCODE_BIN_SH);
        if (enabled) {
            sMainTimer.dispatch("qrcode-server", 1500, []() {
                runCommands("/bin/sh %s start > /dev/null 2>&1", APP_QRCODE_BIN_SH);
            });
        }
        wrteFile(APP_INDUS_QRCODE_FILE, _js.dump());
    }
    HTTP_ResponseDataAsJSON(message, status, js.dump());
}

static void APIV1_CGI_IndustriaEndpoint(FCGX_Request &message, nlohmann::json &js) {
    std::string body = HTTP_ExtractBodyContent(message);
    nlohmann::json _js = nlohmann::json::parse(body);
    {
        /* 
            @We safe certificate in seperate for using CURL
        */
        std::string cert = _js["tls"]["ca_certificate"].get<std::string>();
        _js["tls"]["ca_certificate"].clear();
        wrteFile(APP_INDUS_ENDPOINT_CERT, cert);
        wrteFile(APP_INDUS_ENDPOINT_FILE, _js.dump());
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_RecordsPlaylist(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];
    std::string body = HTTP_ExtractBodyContent(message);
    {
        nlohmann::json _js = nlohmann::json::parse(body);
        std::string datetime = _js["datetime"].get<std::string>();
        const std::string selected = std::string("/mnt/sdcard/") + datetime + std::string("/index.db");
        FILE *fp = fopen(selected.c_str(), "rb");
        if (!fp) {
            js["success"] = false;
            js["message"] = "Empty playlist";
            HTTP_ResponseDataAsJSON(message, 400, js.dump());
            return;
        }

        do {
            RECORDER_INDEX_S item = {0};
            size_t readBytes = fread(&item, sizeof(char), sizeof(RECORDER_INDEX_S), fp);
            if (readBytes == sizeof(RECORDER_INDEX_S)) {
                nlohmann::json sjs;
                sjs["events"] = nlohmann::json::array();
                sjs["name"] = std::string(item.name, strnlen(item.name, sizeof(item.name)));
                for (int id = 0; id < item.metadata.ind; id++) {
                    sjs["events"].push_back(item.metadata.events[id].type);
                }
                data["playlist"].push_back(sjs);
            } else break;
        } while (1);
        fclose(fp);
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

static void APIV1_CGI_RecordsCalendar(FCGX_Request &message, nlohmann::json &js) {
    nlohmann::json &data = js["data"];
    std::string body = HTTP_ExtractBodyContent(message);

    {
        uint32_t mask = 0;
        const std::string MOUNT_POINT = std::string("/mnt/sdcard");
        nlohmann::json _js = nlohmann::json::parse(body);
        int YY = _js["year"].get<int>();
        int MM = _js["month"].get<int>();
        DIR *dir = opendir(MOUNT_POINT.c_str());
        if (dir) {
            struct dirent *ent = NULL;
            while ((ent = readdir(dir)) != nullptr) {
                if (ent->d_type != DT_DIR) {
                    continue;
                }
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                    continue;
                }
                int yy, mm, dd;
                const std::string folder = std::string(ent->d_name, strlen(ent->d_name));
                if (sscanf(folder.c_str(), "%4d-%2d-%2d", &yy, &mm, &dd) != 3) {
                    continue;
                }

                if (yy == YY && mm == MM) {
                    mask |= (1U << (dd - 1));
                }
            }
            closedir(dir);
        }
        data["calendar"] = mask;
    }
    HTTP_ResponseDataAsJSON(message, 200, js.dump());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
HashTableEntrance POST_HashMap[] = {
    /*
        @User
    */
    {(char *)"/api/v1/user/login"               , false ,   Customer      , APIV1_CGI_UserLogin            },
    {(char *)"/api/v1/user/logout"              , true  ,   Customer      , APIV1_CGI_UserLogout           },
    /*
        @Stream
        @Record
    */
    {(char *)"/api/v1/record/playlist"          , true  ,   Customer      , APIV1_CGI_RecordsPlaylist      },
    {(char *)"/api/v1/record/calendar"          , true  ,   Customer      , APIV1_CGI_RecordsCalendar      },
    /*
        @Media
    */
    {(char *)"/api/v1/media/video"              , true  ,   Operator      , APIV1_CGI_MediaVideo           },
    {(char *)"/api/v1/media/audio"              , true  ,   Operator      , APIV1_CGI_MediaAudio           },
    {(char *)"/api/v1/media/image"              , true  ,   Operator      , APIV1_CGI_MediaImage           },
    /*
        @Network
    */
    {(char *)"/api/v1/network/stream"           , true  ,   Operator      , APIV1_CGI_NetworkStream        },
    {(char *)"/api/v1/network/protocols"        , true  ,   Operator      , APIV1_CGI_NetworkProtocols     },
    {(char *)"/api/v1/network/wifi/connect"     , true  ,   Operator      , APIV1_CGI_NetworkWiFiConnect   },
    {(char *)"/api/v1/network/wifi/disconnect"  , true  ,   Operator      , APIV1_CGI_NetworkWiFiDisconnect},
    {(char *)"/api/v1/network/lan/mode"         , true  ,   Operator      , APIV1_CGI_NetworkLanMode       },
    /*
        @System
    */
    {(char *)"/api/v1/system/reboot"            , true  ,   Operator      , APIV1_CGI_SystemReboot         },
    {(char *)"/api/v1/system/reset"             , true  ,   Operator      , APIV1_CGI_SystemReset          },
    {(char *)"/api/v1/system/upgrade"           , true  ,   Administrator , APIV1_CGI_SystemUpgrade        },
    {(char *)"/api/v1/system/time"              , true  ,   Operator      , APIV1_CGI_SystemTime           },
    {(char *)"/api/v1/system/information"       , true  ,   Operator      , APIV1_CGI_SystemInformation    },
    {(char *)"/api/v1/system/users/add"         , true  ,   Administrator , APIV1_CGI_SystemUsersAdd       },
    {(char *)"/api/v1/system/users/update"      , true  ,   Administrator , APIV1_CGI_SystemUsersUpdate    },
    {(char *)"/api/v1/system/users/delete"      , true  ,   Administrator , APIV1_CGI_SystemUsersDelete    },
    /*
        @Storage
    */
    {(char *)"/api/v1/storage/mode"             , true  ,   Operator      , APIV1_CGI_StorageMode          },
    {(char *)"/api/v1/storage/format"           , true  ,   Operator      , APIV1_CGI_StorageFormat        },
    /*
        @GPIO
    */
    {(char *)"/api/v1/gpio/led"                 , true  ,   Operator      , APIV1_CGI_GpioLed              },
    {(char *)"/api/v1/gpio/siren"               , true  ,   Operator      , APIV1_CGI_GpioSiren            },
    {(char *)"/api/v1/gpio/lighting"            , true  ,   Operator      , APIV1_CGI_GpioLighting         },
    {(char *)"/api/v1/gpio/motors"              , true  ,   Operator      , APIV1_CGI_GpioMotors           },
    /*
        @Event
    */
    {(char *)"/api/v1/event/alarm"              , true  ,   Operator      , APIV1_CGI_EventAlarm           },
    {(char *)"/api/v1/event/detection"          , true  ,   Operator      , APIV1_CGI_EventDetection       },
    /*
        @Industrial IO
    */
    {(char *)"/api/v1/industrial/rs485"         , false ,   Operator      , APIV1_CGI_IndustrialRS485      },
    {(char *)"/api/v1/industrial/qr_barcode"    , true  ,   Operator      , APIV1_CGI_IndustriaQRBarcode   },
    {(char *)"/api/v1/industrial/endpoint"      , false ,   Operator      , APIV1_CGI_IndustriaEndpoint    },
    /*
        @End of function
    */
    {(char *)NULL                                , false ,  Customer      , (CGI_FunCallback)NULL          },
};
