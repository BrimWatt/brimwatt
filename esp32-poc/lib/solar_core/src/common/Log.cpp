#include "common/Log.h"

#include <cstdarg>
#include <cstdio>

namespace solar {

namespace {
LogSink sink = nullptr;

const char* label(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        default: return "ERROR";
    }
}
}  // namespace

void setLogSink(LogSink s) { sink = s; }

void logf(LogLevel level, const char* format, ...) {
    char buffer[384];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    if (sink) {
        sink(level, buffer);
    } else {
        std::printf("%s %s\n", label(level), buffer);
    }
}

}  // namespace solar
