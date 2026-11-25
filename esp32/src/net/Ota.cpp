#include "net/Ota.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include "common/Log.h"

namespace device::Ota {

namespace {
unsigned lastPercent = 101;
}

void begin(const char* hostname, const char* password, std::function<void()> beforeUpdate) {
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(password);
    ArduinoOTA.setMdnsEnabled(false);   // main already started mDNS under the same name; just announce the OTA service on it
    ArduinoOTA.onStart([beforeUpdate]() {
        LOG_WARN("OTA: new firmware coming in over Wi-Fi. The board restarts when it is done (about 30 s).");
        lastPercent = 101;
        if (beforeUpdate) beforeUpdate();
    });
    ArduinoOTA.onProgress([](unsigned done, unsigned total) {
        unsigned percent = total ? done * 100 / total : 0;
        if (percent / 20 != lastPercent / 20) {   // a line every 20 %
            lastPercent = percent;
            LOG_INFO("OTA: %u%%", percent);
        }
    });
    ArduinoOTA.onEnd([]() { LOG_WARN("OTA: update received, restarting."); });
    ArduinoOTA.onError([](ota_error_t error) {
        const char* why = error == OTA_AUTH_ERROR      ? "wrong password"
                          : error == OTA_BEGIN_ERROR   ? "could not start (not enough space?)"
                          : error == OTA_CONNECT_ERROR ? "could not connect back to the computer"
                          : error == OTA_RECEIVE_ERROR ? "the transfer broke off"
                          : error == OTA_END_ERROR     ? "the new image did not verify"
                                                       : "unknown";
        LOG_ERROR("OTA: update failed: %s. The current firmware keeps running.", why);
    });
    ArduinoOTA.begin();
    MDNS.enableArduino(3232, true);   // lets tools find the board as brimwatt.local
    LOG_INFO("OTA: ready. Update over Wi-Fi with: pio run -e esp32s3-ota -t upload");
}

void loop() { ArduinoOTA.handle(); }

}  // namespace device::Ota
