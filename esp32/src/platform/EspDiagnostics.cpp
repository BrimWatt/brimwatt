#include "platform/EspDiagnostics.h"

#include <Preferences.h>
#include <WiFi.h>

#include "common/Log.h"

namespace device {

namespace {
constexpr const char* kNamespace = "diag";
}

const char* resetReasonText(esp_reset_reason_t reason, bool& crash) {
    crash = true;
    switch (reason) {
        case ESP_RST_POWERON: crash = false; return "power on (normal start)";
        case ESP_RST_SW: crash = false; return "software restart (normal)";
        case ESP_RST_EXT: crash = false; return "reset pin / RESET button";
        case ESP_RST_DEEPSLEEP: crash = false; return "woke from deep sleep";
        case ESP_RST_PANIC: return "CRASH (panic / exception)";
        case ESP_RST_INT_WDT: return "CRASH (interrupt watchdog: something blocked too long)";
        case ESP_RST_TASK_WDT: return "CRASH (task watchdog: a task blocked too long)";
        case ESP_RST_WDT: return "CRASH (other watchdog)";
        case ESP_RST_BROWNOUT: return "BROWNOUT (the power dipped: check the charger and cable)";
        case ESP_RST_SDIO: return "SDIO reset";
        default: return "unknown";
    }
}

void EspDiagnostics::recordBoot(int64_t nowEpoch) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        LOG_WARN("Diagnostics: could not open flash storage; restarts will not be counted.");
        return;
    }
    bool crash = false;
    const char* why = resetReasonText(esp_reset_reason(), crash);
    restarts_ = prefs.getInt("restarts", 0) + 1;
    crashes_ = prefs.getInt("crashes", 0);
    prefs.putInt("restarts", restarts_);
    if (crash) {
        crashes_++;
        prefs.putInt("crashes", crashes_);
        prefs.putString("crashWhy", why);
        prefs.putLong64("crashAt", nowEpoch);
    }
    lastCrash_ = prefs.getString("crashWhy", "").c_str();
    lastCrashAt_ = prefs.getLong64("crashAt", 0);
    prefs.end();
}

void EspDiagnostics::service(int64_t nowEpoch) {
    watch_.update(static_cast<uint32_t>(millis() / 1000), nowEpoch > 1700000000 ? nowEpoch : 0, static_cast<int>(ESP.getFreeHeap() / 1024),
                  static_cast<int>(ESP.getMinFreeHeap() / 1024), static_cast<int>(ESP.getMaxAllocHeap() / 1024), static_cast<int>(ESP.getFreePsram() / 1024));
}

solar::Diagnostics EspDiagnostics::read() const {
    solar::Diagnostics d;
    d.platform = "ESP32-S3";
    d.uptimeSeconds = static_cast<uint32_t>(millis() / 1000);
    bool crash = false;
    d.lastRestart = resetReasonText(esp_reset_reason(), crash);
    d.lastRestartWasCrash = crash;
    d.restarts = restarts_;
    d.crashes = crashes_;
    d.lastCrashReason = lastCrash_;
    d.lastCrashAt = lastCrashAt_;
    d.freeHeapKb = static_cast<int>(ESP.getFreeHeap() / 1024);
    d.minFreeHeapKb = static_cast<int>(ESP.getMinFreeHeap() / 1024);
    d.largestBlockKb = static_cast<int>(ESP.getMaxAllocHeap() / 1024);
    if (ESP.getPsramSize() > 0) {
        d.freePsramKb = static_cast<int>(ESP.getFreePsram() / 1024);
        d.minFreePsramKb = static_cast<int>(ESP.getMinFreePsram() / 1024);
    }
    d.memoryWatch = &watch_;
    d.hasWifi = WiFi.isConnected();
    d.wifiRssi = d.hasWifi ? WiFi.RSSI() : 0;
    d.firmwareBuilt = built_;
    d.clockSet = time(nullptr) > 1700000000;
    return d;
}

}  // namespace device
