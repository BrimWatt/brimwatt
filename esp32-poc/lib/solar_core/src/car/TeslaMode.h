#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace solar {

/** The name of the board's own Wi-Fi while Tesla mode is on. */
constexpr const char* kTeslaHotspotName = "wattsync-tesla";

struct HotspotInfo {
    bool up = false;
    int clients = 0;
};

/**
 * Tesla mode: one switch for "this board is used with a car". Off (the default) Bluetooth is never started; on, Bluetooth works, the
 * board opens its own Wi-Fi (the hotspot) next to the home Wi-Fi, and the car is in command. Changing it restarts the board.
 */
class TeslaModeControl {
public:
    virtual ~TeslaModeControl() = default;
    virtual bool enabled() const = 0;
    virtual bool restartPending() const = 0;
    /** Saves the choice and restarts the board soon (the restart is what hands Bluetooth's memory back or keeps it in reserve). */
    virtual void setEnabled(bool on) = 0;
    virtual HotspotInfo hotspot() const = 0;
    /** The hotspot's password, made on first use and kept on the board. Only ever sent on request, never in telemetry or logs. */
    virtual std::string hotspotPassword() = 0;
    /** Sets the board's clock (seconds since 1970) when it has none, e.g. away from home with no internet. False if it already has one. */
    virtual bool setClock(int64_t epochSeconds) = 0;
};

/** Ten characters without look-alikes (no 0/O, 1/l/i): easy to type on a phone, far more than WPA2 needs. */
inline std::string generateHotspotPassword(const std::function<uint32_t()>& random32) {
    static const char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    constexpr uint32_t kCount = sizeof(kAlphabet) - 1;
    std::string out;
    for (int i = 0; i < 10; i++) out += kAlphabet[random32() % kCount];
    return out;
}

}  // namespace solar
