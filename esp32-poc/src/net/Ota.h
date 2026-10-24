#pragma once

#include <functional>

namespace device::Ota {

/**
 * Lets the board take a new firmware over Wi-Fi (PlatformIO environment esp32s3-ota), protected by a password. The
 * update runs inside loop(), which blocks for 10 to 20 seconds, and the board restarts when it is done.
 * `beforeUpdate` runs once just before the first byte is written, to save anything that must not be lost.
 */
void begin(const char* hostname, const char* password, std::function<void()> beforeUpdate);

/** Call from the main loop. */
void loop();

}  // namespace device::Ota
