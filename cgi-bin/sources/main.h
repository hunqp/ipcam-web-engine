#ifndef MAIN_H
#define MAIN_H

#include <string>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "utils.h"
#include "json.hpp"
#include "network.h"
#include "fcgiapp.h"
#include "cgi_debug.h"
#include "kiwi_credentials.h"

/* Include ROCKCHIP SDK */
#include "rk_param.h"
#include "rk_gpio.h"
#include "rk_client.h"

#define RAM_ROOT                        "/tmp"
#define WWW_REDIRECT_LOGIN              "/login"
#define WWW_REDIRECT_PREVIEW            "/preview"
#define WWW_INDEX_PAGE                  "/index.html"

/*
    @User configuration - This is applied for all process/applications
*/
#define APP_USERDATA_DIR				"/userdata"
#define APP_USER_BIN_DIR                "/oem/usr/bin"
#define APP_INTEGRATION_DIR             APP_USERDATA_DIR "/usr-customize"
/*
    @User default/runtime configurations
*/
#define APP_VERSION_FILE                APP_USERDATA_DIR "/version"
#define APP_IPC_CONFIGURE_FILE          APP_USERDATA_DIR "/rkipc.ini"
#define APP_JOURNAL_LOG_FILE            APP_USERDATA_DIR "/journal.log"
#define APP_WIFI_CONFIGURE_FILE         APP_USERDATA_DIR "/wpa_supplicant.conf"
#define APP_REGISTERED_STATUS_FILE		APP_USERDATA_DIR "/registered"
#define APP_UNIQUE_SERIAL_FILE		    APP_USERDATA_DIR "/serial-number"
#define APP_ACCOUNTS_DB_FILE            APP_USERDATA_DIR "/accounts.db"
#define APP_SECRET_UNIQUE_FILE          APP_USERDATA_DIR "/secret-unique"
/*
    @User customize configurations for intergration
*/
#define APP_RTMP_CONFIGURE_FILE         APP_INTEGRATION_DIR "/rtmp-stream.json"
#define APP_STORAGE_CONFIGURE_FILE      APP_INTEGRATION_DIR "/storage.json"
#define APP_EVENT_DISPATCHER_FILE       APP_INTEGRATION_DIR "/event-dispatcher.json"
#define APP_EVENT_DISPATCHER_CERT       APP_INTEGRATION_DIR "/event-dispatcher.cert"
#define APP_INDUS_RS485_FILE            APP_INTEGRATION_DIR "/rs485.json"
#define APP_INDUS_QRCODE_FILE           APP_INTEGRATION_DIR "/qr-barcode.json"
#define APP_INDUS_ENDPOINT_FILE         APP_INTEGRATION_DIR "/endpoint.json"
#define APP_INDUS_ENDPOINT_CERT         APP_INTEGRATION_DIR "/endpoint.cert"
/*
    @Extension applications/scripts
*/
#define APP_RTMP_BIN_SH                 APP_USER_BIN_DIR "/app_rtmp.sh"
#define APP_MODBUS_BIN_SH               APP_USER_BIN_DIR "/app_modbus.sh"
#define APP_QRCODE_BIN_SH               APP_USER_BIN_DIR "/app_qrcode.sh"

/*-------------------------------------------------------------------------*/

typedef void (*CGI_FunCallback)(FCGX_Request& message, nlohmann::json& js);

typedef enum {
    Administrator = 0,
    Operator,
    Customer,
} eUserLevels;

typedef struct {
    char *api;
    bool needToAuthenticate;
    eUserLevels role;
    CGI_FunCallback callback;
} HashTableEntrance;

/**/
typedef struct {
    uint32_t beginTS;
    uint32_t endTS;
    uint16_t offsetSeconds;
    uint8_t type;
} RECORDER_METADATA_S;

typedef struct {
    uint8_t ind;
    RECORDER_METADATA_S events[5];
} RECORDER_LIST_METADATA_S;

typedef struct {
    char name[32];
    uint32_t startTs;
    uint32_t closeTs;
    RECORDER_LIST_METADATA_S metadata;
} RECORDER_INDEX_S;
/**/

extern std::string WWW_ROOT;
extern HashTableEntrance GET_HashMap[];
extern HashTableEntrance PUT_HashMap[];
extern HashTableEntrance POST_HashMap[];
extern HashTableEntrance UPDATE_HashMap[];
extern HashTableEntrance DELETE_HashMap[];

#endif /* MAIN_H */
