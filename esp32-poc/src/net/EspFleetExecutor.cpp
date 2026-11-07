#include "net/EspFleetExecutor.h"

#include <Arduino.h>

#include "common/Log.h"

namespace device {

namespace {
void run(void* arg) {
    std::function<void()>* job = static_cast<std::function<void()>*>(arg);
    (*job)();
    delete job;
    vTaskDelete(nullptr);
}
}  // namespace

bool EspFleetExecutor::start(std::function<void()> job) {
    std::function<void()>* heap = new std::function<void()>(std::move(job));
    // TLS plus the protocol flows need a lot of stack; 20 KB has room. Priority 1, like the notification sender.
    if (xTaskCreate(&run, "fleet", 20480, heap, 1, nullptr) != pdPASS) {
        delete heap;
        LOG_WARN("Tesla: no memory for a job task (free heap %u KB)", static_cast<unsigned>(ESP.getFreeHeap() / 1024));
        return false;
    }
    return true;
}

void EspFleetExecutor::sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

}  // namespace device
