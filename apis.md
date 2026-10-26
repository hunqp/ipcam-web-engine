# VIVOO Camera — HTTP API Reference

All JSON responses share this envelope:

```json
{ "success": true, "message": "Accepted", "timestamp": 1778171000, "data": {} }
```

## Table of Contents

- **[1. USER](#1-user)**
  - [`POST` /api/v1/user/login](#1-post-apiv1userlogin)
  - [`POST` /api/v1/user/logout](#2-post-apiv1userlogout)
  - [`GET ` /activate](#3-get-activate)
- **[2. MEDIA](#2-media)**
  - [`GET ` /api/v1/media/snapshot](#4-get-apiv1mediasnapshot)
  - [`GET ` /api/v1/media/video](#5-get-apiv1mediavideo)
  - [`POST` /api/v1/media/video](#6-post-apiv1mediavideo)
  - [`GET ` /api/v1/media/audio](#7-get-apiv1mediaaudio)
  - [`POST` /api/v1/media/audio](#8-post-apiv1mediaaudio)
  - [`GET ` /api/v1/media/image](#9-get-apiv1mediaimage)
  - [`POST` /api/v1/media/image](#10-post-apiv1mediaimage)
- **[3. NETWORK](#3-network)**
  - [`GET ` /api/v1/network/protocols](#11-get-apiv1networkprotocols)
  - [`POST` /api/v1/network/protocols](#12-post-apiv1networkprotocols)
  - [`GET ` /api/v1/network/status](#13-get-apiv1networkstatus)
  - [`GET ` /api/v1/network/wifi/scan](#14-get-apiv1networkwifiscan)
  - [`POST` /api/v1/network/wifi/connect](#15-post-apiv1networkwificonnect)
  - [`POST` /api/v1/network/wifi/disconnect](#16-post-apiv1networkwifidisconnect)
  - [`POST` /api/v1/network/lan/mode](#17-post-apiv1networklanmode)
  - [`GET ` /api/v1/network/stream](#18-get-apiv1networkstream)
  - [`POST` /api/v1/network/stream](#19-post-apiv1networkstream)
- **[4. SYSTEM](#4-system)**
  - [`POST` /api/v1/system/reboot](#20-post-apiv1systemreboot)
  - [`POST` /api/v1/system/reset](#21-post-apiv1systemreset)
  - [`POST` /api/v1/system/upgrade](#22-post-apiv1systemupgrade)
  - [`POST` /api/v1/system/time](#23-post-apiv1systemtime)
  - [`GET ` /api/v1/system/time](#24-get-apiv1systemtime)
  - [`POST` /api/v1/system/information](#25-post-apiv1systeminformation)
  - [`GET ` /api/v1/system/information](#26-get-apiv1systeminformation)
  - [`GET ` /api/v1/system/users/list](#27-get-apiv1systemuserslist)
  - [`POST` /api/v1/system/users/add](#28-post-apiv1systemusersadd)
  - [`POST` /api/v1/system/users/update](#29-post-apiv1systemusersupdate)
  - [`POST` /api/v1/system/users/delete](#30-post-apiv1systemusersdelete)
- **[5. STORAGE](#5-storage)**
  - [`POST` /api/v1/storage/mode](#31-post-apiv1storagemode)
  - [`POST` /api/v1/storage/format](#32-post-apiv1storageformat)
  - [`GET ` /api/v1/storage/mode](#33-get-apiv1storagemode)
  - [`GET ` /api/v1/storage/status](#34-get-apiv1storagestatus)
- **[6. GPIO](#6-gpio)**
  - [`GET ` /api/v1/gpio/status](#35-get-apiv1gpiostatus)
  - [`POST` /api/v1/gpio/led](#36-post-apiv1gpioled)
  - [`POST` /api/v1/gpio/siren](#37-post-apiv1gpiosiren)
  - [`POST` /api/v1/gpio/lighting](#38-post-apiv1gpiolighting)
  - [`POST` /api/v1/gpio/motors](#39-post-apiv1gpiomotors)
- **[7. EVENT](#7-event)**
  - [`GET ` /api/v1/event/detection](#40-get-apiv1eventdetection)
  - [`POST` /api/v1/event/detection](#41-post-apiv1eventdetection)
  - [`GET ` /api/v1/event/alarm](#42-get-apiv1eventalarm)
  - [`POST` /api/v1/event/alarm](#43-post-apiv1eventalarm)
- **[8. LIVESTREAM / PLAYBACK](#8-livestream--playback)**
  - [`POST` /api/v1/record/calendar](#44-post-apiv1recordcalendar)
  - [`POST` /api/v1/record/playlist](#45-post-apiv1recordplaylist)
- **[9. INDUSTRIAL IO](#9-industrial-io)**
  - [`GET ` /api/v1/industrial/rs485](#46-get-apiv1industrialrs485)
  - [`POST` /api/v1/industrial/rs485](#47-post-apiv1industrialrs485)
  - [`GET ` /api/v1/industrial/qr_barcode](#48-get-apiv1industrialqr_barcode)
  - [`POST` /api/v1/industrial/qr_barcode](#49-post-apiv1industrialqr_barcode)
  - [`POST` /api/v1/industrial/endpoint](#50-post-apiv1industrialendpoint)
  - [`GET ` /api/v1/industrial/endpoint](#51-get-apiv1industrialendpoint)

---

## 1. USER

### 1. POST /api/v1/user/login

**Request:**

```text
username=admin&password=admin
```

**Response:**

```http
HTTP/1.1 200 OK
Set-Cookie: vivoo_session={jwt}; Path=/; Max-Age=3600; HttpOnly; SameSite=Strict
```

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "role": 0,
        "redirect": "/preview",
        "username": "admin",
        "permissions": {
            "liveview": true,
            "playback": true,
            "network": {
                "wifiSettings": true,
                "lanSettings": true,
                "protocolSettings": true,
                "rtmpSettings": true
            },
            "video": {"mainStreamSettings": true, "subStreamSettings": true},
            "audioSettings": true,
            "image": {
                "displaySettings": true,
                "osdSettings": true,
                "privacyMaskSettings": true,
                "lightingSettings": true
            },
            "storageSettings": true,
            "event": {"smartDetectionSettings": true, "alarmSettings": true},
            "industrialIo": {
                "rs485Settings": true,
                "qrBarcodeSettings": true,
                "integrationSettings": true
            },
            "system": {
                "accountSettings": true,
                "timeSettings": true,
                "generalSettings": true,
                "systemInformation": true,
                "firmwareUpgrade": true
            }
        }
    }
}
```

### 2. POST /api/v1/user/logout

**Request:** _(none)_

**Response:**

```http
HTTP/1.1 200 OK
Set-Cookie: vivoo_session=; Max-Age=0; HttpOnly
```

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {"redirect": "/login"}
}
```

### 3. GET /activate

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {"activate": true}
}
```

---

## 2. MEDIA

### 4. GET /api/v1/media/snapshot

**Request:** _(none)_

**Response:**

```http
HTTP/1.1 200 OK
Content-Type: image/jpeg
```

> [Binary image data]

> ⚠ No JSON body is returned

### 5. GET /api/v1/media/video

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "mainStream": {
            "width": 1920,
            "height": 1080,
            "fps": 30,
            "gop": 30,
            "minBitrate": 512,
            "maxBitrate": 4096,
            "rcMode": "CBR",
            "rcQuality": 4,
            "encodeType": "H264",
            "minQp": 20,
            "maxQp": 50
        },
        "minorStream": {
            "width": 640,
            "height": 360,
            "fps": 15,
            "gop": 30,
            "minBitrate": 128,
            "maxBitrate": 1024,
            "rcMode": "CBR",
            "rcQuality": 2,
            "encodeType": "H264",
            "minQp": 20,
            "maxQp": 50
        }
    }
}
```

### 6. POST /api/v1/media/video

**Request:**

```json
{
    "mainStream": {
        "width": 1920,
        "height": 1080,
        "fps": 30,
        "gop": 30,
        "minBitrate": 512,
        "maxBitrate": 4096,
        "rcMode": "CBR",
        "rcQuality": 4,
        "encodeType": "H264",
        "minQp": 20,
        "maxQp": 50
    },
    "minorStream": {
        "width": 640,
        "height": 360,
        "fps": 15,
        "gop": 30,
        "minBitrate": 128,
        "maxBitrate": 1024,
        "rcMode": "CBR",
        "rcQuality": 2,
        "encodeType": "H264",
        "minQp": 20,
        "maxQp": 50
    }
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 7. GET /api/v1/media/audio

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "microphone": {
            "volume": 100,
            "gain": 20,
            "echoCancellation": false,
            "noiseSuppression": false
        },
        "speaker": {
            "volume": 100,
            "softwareAmplifier": false,
            "softwareAmplifierValue": 1.5
        }
    }
}
```

### 8. POST /api/v1/media/audio

**Request:**

```json
{
    "microphone": {
        "volume": 100,
        "gain": 20,
        "echoCancellation": false,
        "noiseSuppression": false
    },
    "speaker": {
        "volume": 100,
        "softwareAmplifier": false,
        "softwareAmplifierValue": 1.5
    }
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 9. GET /api/v1/media/image

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "isp": {
            "hue": 50,
            "contrast": 50,
            "sharpness": 50,
            "brightness": 50,
            "saturation": 50,
            "flipMirror": 0,
            "antiFlicker": 50,
            "antiFogging": true,
            "wdr": 0,
            "whiteBalance": {
                "mode": 0,
                "red": 50,
                "green": 50,
                "blue": 50
            },
            "exposure": {
                "mode": 0,
                "time": "1/25",
                "gainMode": "auto",
                "gain": 1
            },
            "noiseReduction": {"mode": "3dnr", "level": 50}
        },
        "smartIr": {"switchContext": 0, "irDimmer": 60},
        "osd": {
            "fontSize": 32,
            "fontColor": "#ffffff",
            "datetime": {
                "enabled": true,
                "positionX": 16,
                "positionY": 440
            },
            "watermark": {
                "enabled": true,
                "content": "C340-VIVOO-PROD",
                "positionX": 16,
                "positionY": 440
            },
            "imageLogo": {
                "enabled": true,
                "positionX": 16,
                "positionY": 440
            },
            "rs485": {
                "enabled": false,
                "positionX": 16,
                "positionY": 440
            }
        },
        "privacyMasks": [
            {
                "enabled": false,
                "positionX": 0,
                "positionY": 0,
                "width": 0,
                "height": 0
            }
        ]
    }
}
```

### 10. POST /api/v1/media/image

**Request:**

```json
{
    "isp": {
        "hue": 50,
        "contrast": 50,
        "sharpness": 50,
        "brightness": 50,
        "saturation": 50,
        "flipMirror": 0,
        "antiFlicker": 50,
        "antiFogging": true,
        "wdr": 0,
        "whiteBalance": {
            "mode": 0,
            "red": 50,
            "green": 50,
            "blue": 50
        },
        "exposure": {
            "mode": 0,
            "time": "1/25",
            "gainMode": "auto",
            "gain": 1
        },
        "noiseReduction": {"mode": "3dnr", "level": 50}
    },
    "smartIr": {"switchContext": 0, "irDimmer": 60},
    "osd": {
        "fontSize": 32,
        "fontColor": "#ffffff",
        "datetime": {
            "enabled": true,
            "positionX": 16,
            "positionY": 440
        },
        "watermark": {
            "enabled": true,
            "content": "C340-VIVOO-PROD",
            "positionX": 16,
            "positionY": 440
        },
        "imageLogo": {
            "enabled": true,
            "positionX": 16,
            "positionY": 440
        },
        "rs485": {
            "enabled": false,
            "positionX": 16,
            "positionY": 440
        }
    },
    "privacyMasks": [
        {
            "enabled": false,
            "positionX": 0,
            "positionY": 0,
            "width": 0,
            "height": 0
        }
    ]
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

---

## 3. NETWORK

### 11. GET /api/v1/network/protocols

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "http": {"port": 443},
        "rtsp": {
            "port": 554,
            "tls": false,
            "enabled": true
        },
        "onvif": {"enabled": true, "tls": false}
    }
}
```

### 12. POST /api/v1/network/protocols

**Request:**

```json
{
    "http": {"port": 443},
    "rtsp": {
        "port": 554,
        "tls": false,
        "enabled": true
    },
    "onvif": {"enabled": true, "tls": false}
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000
}
```

### 13. GET /api/v1/network/status

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "wifi": {
            "connected": true,
            "ssid": "VIVOOTECH_2.4G",
            "ipAddress": "192.168.1.100",
            "subnetMask": "255.255.255.0",
            "gateway": "192.168.1.1",
            "dns": ["8.8.8.8", "1.1.1.1"]
        },
        "lan": {
            "connected": true,
            "dhcp": true,
            "macAddress": "00:0C:29:39:EE:90",
            "ipAddress": "192.168.1.100",
            "subnetMask": "255.255.255.0",
            "gateway": "192.168.1.1",
            "dns": ["8.8.8.8", "1.1.1.1"]
        }
    }
}
```

### 14. GET /api/v1/network/wifi/scan

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "list": [
            {
                "ssid": "VIVOOTECH_2.4G",
                "bssid": "46:85:da:23:70:9f",
                "strength": -43,
                "security": "WPA/WPA2-PSK-CCMP"
            }
        ]
    }
}
```

### 15. POST /api/v1/network/wifi/connect

**Request:**

```json
{
    "ssid": "VIVOOTECH_2.4G",
    "password": "1234567890",
    "security": "WPA/WPA2-PSK-CCMP"
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 16. POST /api/v1/network/wifi/disconnect

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 17. POST /api/v1/network/lan/mode

**Request:**

```json
{
    "dhcp": false,
    "ipAddress": "192.168.1.200",
    "subnetMask": "255.255.255.0",
    "gateway": "192.168.1.1",
    "dns": ["8.8.8.8", "1.1.1.1"]
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 18. GET /api/v1/network/stream

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {"enabled": true, "endpoint": "rtmps://your-rtmp-server.com:1443/live"}
}
```

### 19. POST /api/v1/network/stream

**Request:**

```json
{
    "enabled": true,
    "channel": 1,
    "endpoint": "rtmps://your-rtmp-server.com:1443/live"
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

---

## 4. SYSTEM

### 20. POST /api/v1/system/reboot

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 21. POST /api/v1/system/reset

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 22. POST /api/v1/system/upgrade

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 23. POST /api/v1/system/time

**Request:**

```json
{
    "timezone": "Asia/Ho_Chi_Minh",
    "utcOffset": "UTC+07:00",
    "date": "2026-05-11",
    "time": "11:38:22",
    "ntp": {"enabled": true, "server": "pool.ntp.org"}
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 24. GET /api/v1/system/time

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "timezone": "Asia/Ho_Chi_Minh",
        "utcOffset": "UTC+07:00",
        "date": "2026-05-11",
        "time": "11:38:22",
        "ntp": {"enabled": true, "server": "pool.ntp.org"}
    }
}
```

### 25. POST /api/v1/system/information

**Request:**

```json
{"serialNumber": "C340-0703-00000000999"}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 26. GET /api/v1/system/information

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "model": "VVC-C340",
        "manufacturer": "VIVOO TECHNOLOGY CO., LTD",
        "serialNumber": "C3I5-0703-00000000999",
        "firmwareVersion": "1.0.0",
        "hardwareVersion": "1.0.0",
        "releaseDatetime": "2026-09-07T23.18.18Z"
    }
}
```

### 27. GET /api/v1/system/users/list

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "lits": [
            {"username": "admin", "role": 0},
            {"username": "operator", "role": 1},
            {"username": "customer", "role": 2}
        ]
    }
}
```

### 28. POST /api/v1/system/users/add

**Request:**

```json
{
    "username": "admin",
    "password": "admin",
    "role": 0
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 29. POST /api/v1/system/users/update

**Request:**

```json
{
    "username": "admin",
    "password": "admin",
    "role": 0
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 30. POST /api/v1/system/users/delete

**Request:**

```json
{"username": "admin", "role": 0}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

---

## 5. STORAGE

### 31. POST /api/v1/storage/mode

**Request:**

```json
{
    "mode": 0,
    "encryption": false,
    "schedule": {
        "MON": {
            "enabled": false,
            "slots": [
                {"start": "09:22", "end": "11:27"},
                {"start": "17:26", "end": "17:27"}
            ]
        },
        "TUE": {"enabled": true, "slots": []},
        "WED": {"enabled": false, "slots": []},
        "THU": {"enabled": false, "slots": []},
        "FRI": {"enabled": false, "slots": []},
        "SAT": {"enabled": false, "slots": []},
        "SUN": {"enabled": false, "slots": []}
    }
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 32. POST /api/v1/storage/format

**Request:**

```json
{"enabled": true}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 33. GET /api/v1/storage/mode

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "mode": 0,
        "encryption": false,
        "schedule": {
            "MON": {
                "enabled": false,
                "slots": [
                    {"start": "09:22", "end": "11:27"},
                    {"start": "17:26", "end": "17:27"}
                ]
            },
            "TUE": {"enabled": true, "slots": []},
            "WED": {"enabled": false, "slots": []},
            "THU": {"enabled": false, "slots": []},
            "FRI": {"enabled": false, "slots": []},
            "SAT": {"enabled": false, "slots": []},
            "SUN": {"enabled": false, "slots": []}
        }
    }
}
```

### 34. GET /api/v1/storage/status

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "capacity": 33554432,
        "used": 3145728,
        "free": 30408704
    }
}
```

---

## 6. GPIO

### 35. GET /api/v1/gpio/status

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "spotlightStatus": true,
        "sirenStatus": false,
        "motorsPosition": {"horizone": 21, "vertical": 324}
    }
}
```

### 36. POST /api/v1/gpio/led

**Request:**

```json
{"enabled": true}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 37. POST /api/v1/gpio/siren

**Request:**

```json
{"enabled": true}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 38. POST /api/v1/gpio/lighting

**Request:**

```json
{
    "mode": 0,
    "dimmer": 30,
    "schedule": {
        "MON": {
            "enabled": false,
            "slots": [
                {"start": "09:22", "end": "11:27"},
                {"start": "17:26", "end": "17:27"}
            ]
        },
        "TUE": {"enabled": true, "slots": []},
        "WED": {"enabled": false, "slots": []},
        "THU": {"enabled": false, "slots": []},
        "FRI": {"enabled": false, "slots": []},
        "SAT": {"enabled": false, "slots": []},
        "SUN": {"enabled": false, "slots": []}
    }
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 39. POST /api/v1/gpio/motors

**Request:**

```json
{"direction": 0}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

---

## 7. EVENT

### 40. GET /api/v1/event/detection

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "enabled": true,
        "objectDetection": {
            "type": 0,
            "boundingBox": true,
            "sensitivityLevel": 3
        },
        "regionOfInterest": {
            "rule": 0,
            "shape": "polygon",
            "points": [
                {"positionX": 1000, "positionY": 1200},
                {"positionX": 8500, "positionY": 1200},
                {"positionX": 8200, "positionY": 7200},
                {"positionX": 1300, "positionY": 7000}
            ]
        }
    }
}
```

### 41. POST /api/v1/event/detection

**Request:**

```json
{
    "enabled": true,
    "objectDetection": {
        "type": 0,
        "boundingBox": true,
        "sensitivityLevel": 3
    },
    "regionOfInterest": {
        "rule": 0,
        "shape": "polygon",
        "points": [
            {"positionX": 1000, "positionY": 1200},
            {"positionX": 8500, "positionY": 1200},
            {"positionX": 8200, "positionY": 7200},
            {"positionX": 1300, "positionY": 7000}
        ]
    }
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 42. GET /api/v1/event/alarm

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "enabled": true,
        "alarm": {
            "intervalMs": 15000,
            "upload": {
                "protocol": "http",
                "endpoint": "https://customer-server.com/api/device-data",
                "port": 8989,
                "tls": {
                    "enabled": true,
                    "verifyPeer": true,
                    "caCertificate": ""
                }
            },
            "action": {"sound": true, "lighting": true}
        },
        "schedule": {
            "MON": {
                "enabled": false,
                "slots": [
                    {"start": "09:22", "end": "11:27"},
                    {"start": "17:26", "end": "17:27"}
                ]
            },
            "TUE": {"enabled": true, "slots": []},
            "WED": {"enabled": false, "slots": []},
            "THU": {"enabled": false, "slots": []},
            "FRI": {"enabled": false, "slots": []},
            "SAT": {"enabled": false, "slots": []},
            "SUN": {"enabled": false, "slots": []}
        }
    }
}
```

### 43. POST /api/v1/event/alarm

**Request:**

```json
{
    "enabled": true,
    "alarm": {
        "intervalMs": 15000,
        "upload": {
            "protocol": "http",
            "endpoint": "https://customer-server.com/api/device-data",
            "port": 8989,
            "tls": {
                "enabled": true,
                "verifyPeer": true,
                "caCertificate": ""
            }
        },
        "action": {"sound": true, "lighting": false}
    },
    "schedule": {
        "MON": {
            "enabled": false,
            "slots": [
                {"start": "09:22", "end": "11:27"},
                {"start": "17:26", "end": "17:27"}
            ]
        },
        "TUE": {"enabled": true, "slots": []},
        "WED": {"enabled": false, "slots": []},
        "THU": {"enabled": false, "slots": []},
        "FRI": {"enabled": false, "slots": []},
        "SAT": {"enabled": false, "slots": []},
        "SUN": {"enabled": false, "slots": []}
    }
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

---

## 8. LIVESTREAM / PLAYBACK

### 44. POST /api/v1/record/calendar

**Request:**

```json
{"year": 2025, "month": 5}
```

**Response:**

```json
{
    "message": "Accepted",
    "success": true,
    "timestamp": 1778829342,
    "data": {"calendar": 909275186}
}
```

### 45. POST /api/v1/record/playlist

**Request:**

```json
{"datetime": "2025-05-13"}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "playlist": [
            {
                "name": "2026-05-13T16.09.04.mp4",
                "events": [2]
            },
            {
                "name": "2026-05-13T16.09.04.mp4",
                "events": [1]
            }
        ]
    }
}
```

---

## 9. INDUSTRIAL IO

### 46. GET /api/v1/industrial/rs485

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "enabled": true,
        "configuration": {
            "type": "rtu",
            "baudrate": 115200,
            "databits": 8,
            "stopbits": 1,
            "parity": "N",
            "hardwareFlowControl": false,
            "pollingIntervalMs": 15000
        },
        "devices": [
            {
                "id": "sens-01",
                "name": "ES35-SW",
                "slaveId": 1,
                "enabled": true,
                "registers": [
                    {
                        "name": "Temperature value",
                        "functionCode": 4,
                        "address": 0,
                        "quantity": 1,
                        "byteOrder": "ABCD",
                        "dataType": "uint16",
                        "factorScale": 0.0,
                        "unit": "*C"
                    },
                    {
                        "name": "Humidity value",
                        "functionCode": 4,
                        "address": 1,
                        "quantity": 1,
                        "byteOrder": "ABCD",
                        "dataType": "uint16",
                        "factorScale": 0,
                        "unit": "RH"
                    }
                ]
            }
        ]
    }
}
```

### 47. POST /api/v1/industrial/rs485

**Request:**

```json
{
    "enabled": true,
    "configuration": {
        "type": "rtu",
        "baudrate": 115200,
        "databits": 8,
        "stopbits": 1,
        "parity": "N",
        "hardwareFlowControl": false,
        "pollingIntervalMs": 15000
    },
    "devices": [
        {
            "id": "sens-01",
            "name": "ES35-SW",
            "slaveId": 1,
            "enabled": true,
            "registers": [
                {
                    "name": "Temperature value",
                    "functionCode": 4,
                    "address": 0,
                    "quantity": 1,
                    "byteOrder": "ABCD",
                    "dataType": "uint16",
                    "factorScale": 0.0,
                    "unit": "*C"
                },
                {
                    "name": "Humidity value",
                    "functionCode": 4,
                    "address": 1,
                    "quantity": 1,
                    "byteOrder": "ABCD",
                    "dataType": "uint16",
                    "factorScale": 0,
                    "unit": "RH"
                }
            ]
        }
    ]
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 48. GET /api/v1/industrial/qr_barcode

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "enabled": true,
        "configuration": {
            "height": 235,
            "width": 239,
            "channel": 0,
            "positionX": 314,
            "positionY": 278,
            "pollingIntervalMs": 15000
        }
    }
}
```

### 49. POST /api/v1/industrial/qr_barcode

**Request:**

```json
{
    "enabled": true,
    "configuration": {
        "height": 235,
        "width": 239,
        "positionX": 314,
        "positionY": 278,
        "channel": 0,
        "pollingIntervalMs": 15000
    }
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 50. POST /api/v1/industrial/endpoint

**Request:**

```json
{
    "enabled": true,
    "protocol": "http",
    "endpoint": "https://customer-server.com/api/device-data",
    "port": 8989,
    "reportPeriodicMs": 15000,
    "tls": {
        "enabled": true,
        "verifyPeer": true,
        "caCertificate": ""
    }
}
```

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {}
}
```

### 51. GET /api/v1/industrial/endpoint

**Request:** _(none)_

**Response:**

```json
{
    "success": true,
    "message": "Accepted",
    "timestamp": 1778171000,
    "data": {
        "enabled": true,
        "protocol": "http",
        "endpoint": "https://customer-server.com/api/device-data",
        "port": 8989,
        "reportPeriodicMs": 15000,
        "tls": {
            "enabled": true,
            "verifyPeer": true,
            "caCertificate": ""
        }
    }
}
```
