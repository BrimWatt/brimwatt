#include "enphase/MeterPoller.h"

#include <Arduino.h>

namespace device {

void MeterPoller::start() {
    // The TLS handshake needs a generous stack.
    xTaskCreatePinnedToCore(&MeterPoller::run, "meter-poller", 12288, this, 1, &task_, 0);
}

void MeterPoller::request() {
    if (busy_.exchange(true)) return;   // a reading is already being taken
    if (task_) xTaskNotifyGive(task_);
}

void MeterPoller::run(void* self) {
    auto* poller = static_cast<MeterPoller*>(self);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // sleep until a reading is requested

        solar::SolarController::MeterResult result;
        result.ok = poller->client_.read(result.reading, result.error);
        if (result.ok) {
            poller->ok_++;
            poller->lastSuccess_ = millis();
        } else {
            poller->failed_++;
        }
        {
            std::lock_guard<std::mutex> lock(poller->mutex_);
            if (result.ok) poller->last_ = result.reading;
            poller->pending_ = result;
        }
        poller->busy_ = false;
    }
}

std::optional<solar::SolarController::MeterResult> MeterPoller::takeResult() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = pending_;
    pending_.reset();
    return result;
}

std::optional<solar::MeterReading> MeterPoller::lastReading() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_;
}

}  // namespace device
