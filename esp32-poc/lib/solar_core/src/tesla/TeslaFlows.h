#pragma once

#include <string>
#include <vector>

#include "tesla/TeslaSession.h"

namespace solar {
namespace tesla {

/**
 * Pairing: send the add-key request, then wait while the owner taps their key card on the car's centre console.
 * Pure logic. The caller sends request(), hands every message from the car to onMessage(), and stops on Accepted or Refused
 * (or when its own timer runs out).
 */
class PairFlow {
public:
    enum State { Ready, Waiting, Accepted, Refused };

    PairFlow(Crypto& crypto, const Bytes& publicKey65, int role) : c_(crypto), pub_(publicKey65), role_(role) {}

    /** The one message to send to the car (a RoutableMessage, not yet framed). */
    Bytes request();
    void onMessage(const Bytes& routable);

    State state() const { return state_; }
    /** One plain sentence about where things stand or why it was refused. */
    const std::string& text() const { return text_; }
    /** The car refused because this key is already on its list (a repeat pairing): that is as good as accepted. */
    bool alreadyOnCar() const { return alreadyOnCar_; }

private:
    bool alreadyOnCar_ = false;
    Crypto& c_;
    Bytes pub_;
    int role_;
    Bytes routing_;
    State state_ = Ready;
    std::string text_;
};

/**
 * Read-only status: ask the security computer (works with the car asleep), and if the car is awake open a session with the main
 * computer and read the charge state. Sends nothing that changes the car (no wake, no charge command).
 */
class StatusFlow {
public:
    StatusFlow(Crypto& crypto, const Bytes& privateKey, const Bytes& publicKey65, const std::string& vin)
        : c_(crypto), session_(crypto, privateKey, publicKey65, vin, kDomainInfotainment) {}

    /** The first message to send. */
    Bytes start();
    /** Handles one message from the car; returns the next message to send, or empty when there is nothing to send now. */
    Bytes onMessage(const Bytes& routable, uint32_t nowMs);
    /** Call when nothing came back in time. */
    void timedOut();

    bool finished() const { return stage_ == Done; }
    /** True when the whole read worked (a car that is asleep counts: that is a real answer). */
    bool ok() const { return ok_; }
    const std::vector<std::string>& lines() const { return lines_; }
    const ChargeState& charge() const { return charge_; }

private:
    enum Stage { AwaitStatus, AwaitSession, AwaitReply, Done };
    Bytes envelope(int domain, const Bytes& payload);
    void fail(const std::string& why);
    void addCharge(const ChargeState& s);

    Crypto& c_;
    Session session_;
    Bytes routing_;
    Bytes requestTag_;
    Stage stage_ = AwaitStatus;
    bool ok_ = false;
    int sleep_ = 0;
    std::vector<std::string> lines_;
    ChargeState charge_;
};

}  // namespace tesla
}  // namespace solar
