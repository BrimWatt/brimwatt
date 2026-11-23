#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "control/SolarController.h"
#include "enphase/EnphaseClient.h"

namespace device {

/**
 * Reads the Envoy on its own task so the slow HTTPS call (about 2 s) never holds up the wallbox connection.
 * The main loop asks for a reading with request() and collects the outcome later with takeResult().
 */
class MeterPoller {
public:
    explicit MeterPoller(EnphaseClient& client) : client_(client) {}

    void start();

    /** Asks for one reading. Ignored if one is already in progress. */
    void request();

    /** The outcome of the latest request, once, or nothing if there is none waiting. */
    std::optional<solar::SolarController::MeterResult> takeResult();

    /** Totals since boot, for the status line. */
    uint32_t okCount() const { return ok_; }
    uint32_t failCount() const { return failed_; }
    /** Milliseconds since boot of the last successful reading, or 0 if none yet. */
    uint32_t lastSuccessMillis() const { return lastSuccess_; }
    /** Latest successful reading, if any. */
    std::optional<solar::MeterReading> lastReading() const;

private:
    static void run(void* self);

    EnphaseClient& client_;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> busy_{false};
    std::atomic<uint32_t> ok_{0};
    std::atomic<uint32_t> failed_{0};
    std::atomic<uint32_t> lastSuccess_{0};

    mutable std::mutex mutex_;
    std::optional<solar::SolarController::MeterResult> pending_;
    std::optional<solar::MeterReading> last_;
};

}  // namespace device
