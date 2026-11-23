#pragma once

#include <cstddef>
#include <cstdint>

#include "common/Ring.h"

namespace solar {

/** One look at the memory, in KB. */
struct MemorySample {
    uint16_t freeKb = 0;      // internal heap free
    uint16_t largestKb = 0;   // largest free block of the internal heap
    uint16_t psramKb = 0;     // PSRAM free (0 on a board without)
};

/** The lowest the internal heap has been, and when it got there. */
struct LowestMemory {
    bool known = false;
    bool beforeFirstCheck = false;   // true: it was already that low when watching began (start-up), so the time is only an upper bound
    int minFreeKb = 0;
    uint32_t atUptimeSeconds = 0;
    int64_t atEpoch = 0;             // 0 while the clock was not set
    int freeKb = 0;                  // free heap and largest block at the moment it was noticed
    int largestKb = 0;
};

/**
 * Watches the memory so a dip or a slow leak can be found afterwards. The platform calls update() every few seconds with
 * what it can read; this keeps one sample every 5 minutes for 24 hours (in the big allocator, so PSRAM on the board) and
 * remembers when the lowest-ever free heap was reached. Pure: no hardware, host-tested.
 */
class MemoryWatch {
public:
    static constexpr uint32_t kEverySeconds = 300;
    static constexpr size_t kCapacity = 288;   // 24 hours

    MemoryWatch() : samples_(kCapacity) {}

    /** `minFreeKb` is the platform's own lowest-since-start figure, which only ever goes down. */
    void update(uint32_t uptimeSeconds, int64_t epoch, int freeKb, int minFreeKb, int largestKb, int psramKb) {
        if (!started_ || minFreeKb < lowest_.minFreeKb) {
            lowest_.known = true;
            lowest_.beforeFirstCheck = !started_;
            lowest_.minFreeKb = minFreeKb;
            lowest_.atUptimeSeconds = uptimeSeconds;
            lowest_.atEpoch = epoch;
            lowest_.freeKb = freeKb;
            lowest_.largestKb = largestKb;
        }
        if (!started_ || static_cast<uint32_t>(uptimeSeconds - newestAt_) >= kEverySeconds) {
            samples_.push({clamp(freeKb), clamp(largestKb), clamp(psramKb)});
            newestAt_ = uptimeSeconds;
        }
        started_ = true;
    }

    const LowestMemory& lowest() const { return lowest_; }
    size_t size() const { return samples_.size(); }
    /** 0 is the oldest. */
    const MemorySample& sample(size_t index) const { return samples_.at(index); }
    /** Uptime at which the newest sample was taken. */
    uint32_t newestAtUptimeSeconds() const { return newestAt_; }

private:
    static uint16_t clamp(int kb) { return static_cast<uint16_t>(kb < 0 ? 0 : (kb > 65535 ? 65535 : kb)); }

    Ring<MemorySample> samples_;
    LowestMemory lowest_;
    uint32_t newestAt_ = 0;
    bool started_ = false;
};

}  // namespace solar
