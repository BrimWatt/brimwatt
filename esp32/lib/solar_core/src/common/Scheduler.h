#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "common/Platform.h"

namespace solar {

/** Runs a task once, some time from now. Call tick() often from the main loop. */
class Scheduler {
public:
    explicit Scheduler(const Platform& platform) : platform_(platform) {}

    void after(uint64_t delayMillis, std::function<void()> task) {
        items_.push_back({platform_.nowMillis() + delayMillis, std::move(task)});
    }

    void tick() {
        uint64_t now = platform_.nowMillis();
        std::vector<Item> due;
        for (auto it = items_.begin(); it != items_.end();) {
            if (it->due <= now) {
                due.push_back(std::move(*it));
                it = items_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& item : due) item.task();   // may schedule more work
    }

    size_t pending() const { return items_.size(); }

private:
    struct Item {
        uint64_t due;
        std::function<void()> task;
    };

    const Platform& platform_;
    std::vector<Item> items_;
};

}  // namespace solar
