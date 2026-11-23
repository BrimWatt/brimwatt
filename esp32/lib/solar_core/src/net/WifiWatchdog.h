#pragma once

#include <cstdint>

namespace solar {

/**
 * Decides what to do while the home Wi-Fi is gone, so a board that lost its router rejoins by itself once the router is back.
 * The radio's own auto-reconnect is only the first line: it can give up for good (router restarted, a failed DHCP lease, a scan
 * that missed the network), so after a short grace period this asks for a reconnect, then a fresh join, and now and then a full
 * radio reset. It never restarts the board. Pure logic (no hardware): the firmware does what update() says.
 */
class WifiWatchdog {
public:
    enum class Step {
        None,        ///< connected, or still inside the grace period
        Reconnect,   ///< ask the driver to try the saved network again
        Rejoin,      ///< drop the attempt and start a fresh join
        ResetRadio   ///< switch the radio off and on, then join (every 5th try)
    };

    static constexpr uint32_t kGraceMillis = 15000;       ///< the driver's own auto-reconnect gets this long first
    static constexpr uint32_t kReconnectMillis = 15000;   ///< the first nudge, after the grace period
    static constexpr uint32_t kRetryMillis = 60000;       ///< then a fresh join this often
    static constexpr int kResetEvery = 5;                 ///< every Nth fresh join also resets the radio

    /** Call often with the current link state; returns what to do right now (usually None). */
    Step update(bool connected, uint32_t nowMillis) {
        if (connected) {
            if (down_) {
                recoveredAfterSeconds_ = (nowMillis - lostAt_) / 1000;
                recovered_ = true;
                down_ = false;
            }
            return Step::None;
        }
        if (!down_) {
            down_ = true;
            lostAt_ = nowMillis;
            nextAt_ = nowMillis + kGraceMillis;
            nudged_ = false;
            rejoins_ = 0;
            return Step::None;
        }
        if (static_cast<int32_t>(nowMillis - nextAt_) < 0) return Step::None;
        if (!nudged_) {
            nudged_ = true;
            nextAt_ = nowMillis + kReconnectMillis;
            return Step::Reconnect;
        }
        ++rejoins_;
        nextAt_ = nowMillis + kRetryMillis;
        return rejoins_ % kResetEvery == 0 ? Step::ResetRadio : Step::Rejoin;
    }

    bool isDown() const { return down_; }

    /** True once after the link came back; seconds() says how long it was gone. */
    bool takeRecovered() {
        bool r = recovered_;
        recovered_ = false;
        return r;
    }
    uint32_t outageSeconds() const { return recoveredAfterSeconds_; }

private:
    bool down_ = false;
    bool nudged_ = false;
    bool recovered_ = false;
    int rejoins_ = 0;
    uint32_t lostAt_ = 0;
    uint32_t nextAt_ = 0;
    uint32_t recoveredAfterSeconds_ = 0;
};

}  // namespace solar
