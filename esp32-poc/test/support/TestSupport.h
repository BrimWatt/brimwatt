#pragma once

// Fakes shared by the host-side tests.
#include <unity.h>

#include <string>
#include <vector>

#include "common/Platform.h"
#include "wallbox/Charger.h"

namespace testing {

class FakePlatform : public solar::Platform {
public:
    uint64_t now = 100000;
    int64_t epoch = 1000;
    int offsetSeconds = -7 * 3600;   // Pacific daylight time
    uint32_t counter = 1;

    uint64_t nowMillis() const override { return now; }
    int64_t epochSeconds() const override { return epoch; }
    std::string utcIso() const override { return "2026-10-14T22:00:00.000Z"; }
    uint32_t random32() override { return counter++ * 2654435761u; }
    int utcOffsetSeconds(int64_t) const override { return offsetSeconds; }
};

class FakeLink : public solar::ChargerLink {
public:
    bool open = true;
    std::vector<std::string> sent;
    bool isOpen() const override { return open; }
    void send(const std::string& frame) override { sent.push_back(frame); }
    const std::string& last() const { return sent.back(); }

    int count(const char* part) const {
        int n = 0;
        for (const auto& frame : sent) n += frame.find(part) != std::string::npos;
        return n;
    }
};

inline void check(const std::string& expected, const std::string& actual) {
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), actual.c_str());
}

inline bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

}  // namespace testing
