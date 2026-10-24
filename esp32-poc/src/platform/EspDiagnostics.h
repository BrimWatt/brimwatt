#pragma once

#include <esp_system.h>

#include "web/Diagnostics.h"

namespace device {

/** Why the board last restarted, in words. `crash` is set for anything that was not a normal start (panic, watchdog, brownout). */
const char* resetReasonText(esp_reset_reason_t reason, bool& crash);

/**
 * Remembers across restarts how often the board has restarted, how many of those were crashes, and the last crash
 * (what and when), in flash. A later normal restart therefore cannot hide a crash from the night before.
 */
class EspDiagnostics : public solar::DiagnosticsSource {
public:
    /** `built` is when this firmware was compiled. */
    explicit EspDiagnostics(const char* built) : built_(built) {}

    /** Call once at start-up, after the clock is set: counts this start and records it if it was a crash. */
    void recordBoot(int64_t nowEpoch);

    solar::Diagnostics read() const override;

private:
    const char* built_;
    int restarts_ = -1;
    int crashes_ = -1;
    std::string lastCrash_;
    int64_t lastCrashAt_ = 0;
};

}  // namespace device
