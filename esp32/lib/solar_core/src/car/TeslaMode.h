#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace solar {

/** The name of the board's own Wi-Fi while Tesla mode is on. */
constexpr const char* kTeslaHotspotName = "brimwatt-tesla";

/** Whether the Tesla-mode hotspot is up and how many devices have joined it. */
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
    /** "Connect through": false = Bluetooth (the default), true = the owner's Tesla account over the internet (Fleet API). Read once at start-up. */
    virtual bool apiLink() const { return false; }
    /** Saves the choice and restarts the board soon (Bluetooth is only started when it is chosen). */
    virtual void setApiLink(bool) {}
};

/** Ten characters without look-alikes (no 0/O, 1/l/i): easy to type on a phone, far more than WPA2 needs. */
inline std::string generateHotspotPassword(const std::function<uint32_t()>& random32) {
    static const char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    constexpr uint32_t kCount = sizeof(kAlphabet) - 1;
    std::string out;
    for (int i = 0; i < 10; i++) out += kAlphabet[random32() % kCount];
    return out;
}

/**
 * Tesla mode and charger mode are two separate setups: each keeps its own settings (mode, current, allowance, hours, prices, goal, messages,
 * charger state) and its own history (samples, activity, sessions, savings). Switching restarts the board, so one is chosen at start-up by
 * these names. Shared on purpose: the Wi-Fi/meter/update credentials, the Tesla key and pairing, and the hotspot password (they belong to the board).
 */
inline std::string profileNamespace(bool tesla) { return tesla ? "solar-tesla" : "solar"; }   // NVS names hold up to 15 characters
inline std::string profileFile(bool tesla, const std::string& name) { return (tesla ? "/tesla-" : "/") + name; }

}  // namespace solar
