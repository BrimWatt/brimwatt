#pragma once

namespace solar {

enum class LogLevel { Debug, Info, Warn, Error };

using LogSink = void (*)(LogLevel level, const char* message);

/** Where log lines go. The firmware sets this to the serial port; tests may leave the default (stdout). */
void setLogSink(LogSink sink);

void logf(LogLevel level, const char* format, ...) __attribute__((format(printf, 2, 3)));

}  // namespace solar

#define LOG_DEBUG(...) ::solar::logf(::solar::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::solar::logf(::solar::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ::solar::logf(::solar::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::solar::logf(::solar::LogLevel::Error, __VA_ARGS__)
