#pragma once

#include "car/CarControl.h"

namespace solar {

/**
 * "Connect through: Bluetooth | Tesla account". Both links offer the same CarControl, so the commander, the router and the API talk to this
 * one and it forwards to whichever the owner chose. The choice is read once at start-up (changing it restarts the board), so only one
 * link is ever active and the other one is never started.
 */
class CarLinkSwitch : public CarControl {
public:
    CarLinkSwitch(CarControl& bluetooth, CarControl& account) : ble_(bluetooth), api_(account) {}

    void select(bool account) { account_ = account; }
    bool account() const { return account_; }

    CarSnapshot snapshot() const override { return active().snapshot(); }
    CarResult startPair(const std::string& vin) override { return active().startPair(vin); }
    CarResult startRefresh() override { return active().startRefresh(); }
    CarResult startQuietRefresh() override { return active().startQuietRefresh(); }
    CarResult startStart() override { return active().startStart(); }
    CarResult startStop() override { return active().startStop(); }
    CarResult startSetAmps(int amps) override { return active().startSetAmps(amps); }
    CarResult startWake() override { return active().startWake(); }
    void cancel() override { active().cancel(); }
    void setControlled(bool on) override { active().setControlled(on); }
    void setWatch(bool on) override { active().setWatch(on); }
    void forget() override { active().forget(); }

private:
    CarControl& active() const { return account_ ? api_ : ble_; }
    CarControl& ble_;
    CarControl& api_;
    bool account_ = false;
};

}  // namespace solar
