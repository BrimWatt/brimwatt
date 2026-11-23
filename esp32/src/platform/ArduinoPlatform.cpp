#include "platform/ArduinoPlatform.h"

#include <Arduino.h>
#include <sys/time.h>

#include "common/Log.h"

namespace device {

uint64_t ArduinoPlatform::nowMillis() const { return millis(); }

int64_t ArduinoPlatform::epochSeconds() const { return time(nullptr); }

std::string ArduinoPlatform::utcIso() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm t;
    gmtime_r(&tv.tv_sec, &t);
    char text[32];
    snprintf(text, sizeof text, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, (int)(tv.tv_usec / 1000));
    return text;
}

uint32_t ArduinoPlatform::random32() { return esp_random(); }

int ArduinoPlatform::utcOffsetSeconds(int64_t epochSeconds) const {
    // The zone (with daylight saving) is set by configTzTime(). Reading the offset back: interpret the UTC
    // calendar time as local time and see how far that lands from the real instant.
    time_t t = static_cast<time_t>(epochSeconds);
    struct tm local;
    localtime_r(&t, &local);
    struct tm utc;
    gmtime_r(&t, &utc);
    utc.tm_isdst = local.tm_isdst;
    return static_cast<int>(t - mktime(&utc));
}

namespace {
void writeToSerial(solar::LogLevel level, const char* message) {
    const char* label = level == solar::LogLevel::Debug  ? "DEBUG"
                        : level == solar::LogLevel::Info ? "INFO "
                        : level == solar::LogLevel::Warn ? "WARN "
                                                         : "ERROR";
    Serial.printf("%s %s\n", label, message);
}
}  // namespace

void installSerialLogSink() { solar::setLogSink(writeToSerial); }

}  // namespace device
