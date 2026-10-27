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
    /** Handles one message from the car; returns a message to send, or empty. (One retry as a phone key if the car cannot read the first request.) */
    Bytes onMessage(const Bytes& routable);

    State state() const { return state_; }
    /** The form factor the last request asked for (9 cloud key, 7 phone key after the retry). */
    int formFactor() const { return formFactor_; }
    int lastFault() const { return lastFault_; }
    /** One plain sentence about where things stand or why it was refused. */
    const std::string& text() const { return text_; }
    /** The car refused because this key is already on its list (a repeat pairing): that is as good as accepted. */
    bool alreadyOnCar() const { return alreadyOnCar_; }

private:
    bool alreadyOnCar_ = false;
    int formFactor_ = kFormFactorCloudKey;
    bool retried_ = false;
    int lastFault_ = 0;
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

/** Plain-words lines for a charge state (shared by status and the charge commands). */
void describeCharge(const ChargeState& s, std::vector<std::string>& lines);

/**
 * One charge command (start, stop, or set the current). Unlike StatusFlow this CHANGES the car. It never wakes a sleeping car, sends
 * nothing when the amps are out of range, sends exactly one command, and then reads the charge state back so the owner sees what
 * the car now says. A refusal or a silence is reported plainly; after silence the outcome is unknown and the text says so.
 */
class ChargeCommandFlow {
public:
    enum Command { Start, Stop, SetAmps };
    static constexpr int kMinAmps = 6, kMaxAmps = 40;

    ChargeCommandFlow(Crypto& crypto, const Bytes& privateKey, const Bytes& publicKey65, const std::string& vin, Command command, int amps)
        : c_(crypto), session_(crypto, privateKey, publicKey65, vin, kDomainInfotainment), command_(command), amps_(amps) {}

    /** The first message to send, or empty when the request was refused up front (see lines()). */
    Bytes start();
    Bytes onMessage(const Bytes& routable, uint32_t nowMs);
    void timedOut();

    bool finished() const { return stage_ == Done; }
    /** True when the car accepted the command (a failed read-back afterwards does not change that). */
    bool ok() const { return ok_; }
    const std::vector<std::string>& lines() const { return lines_; }
    const ChargeState& charge() const { return charge_; }

private:
    enum Stage { AwaitStatus, AwaitSession, AwaitCommand, AwaitVerify, Done };
    Bytes envelope(int domain, const Bytes& payload);
    Bytes sendCar(const Bytes& payload, uint32_t nowMs);
    std::string doneText() const;
    void fail(const std::string& why);

    Crypto& c_;
    Session session_;
    Command command_;
    int amps_;
    Bytes routing_;
    Bytes requestTag_;
    Stage stage_ = AwaitStatus;
    bool ok_ = false;
    std::vector<std::string> lines_;
    ChargeState charge_;
};

}  // namespace tesla
}  // namespace solar
