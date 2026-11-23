// Copy this file to secrets.h and fill it in. secrets.h is git-ignored.
#pragma once

// 2.4 GHz network only; the ESP32-S3 cannot join 5 GHz.
#define WIFI_SSID     "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// The same access token (JWT) the Java app uses as ENPHASE_TOKEN.
#define ENPHASE_TOKEN "paste-your-enphase-token-here"

// Leave empty to find the gateway as envoy.local (mDNS). If that fails on your network,
// put the gateway's IP address here, for example "192.168.1.50".
#define ENVOY_IP ""

// Password for updating the board over Wi-Fi (pio run -e esp32s3-ota -t upload). Pick anything long and random.
#define OTA_PASSWORD "choose-a-long-random-password"
