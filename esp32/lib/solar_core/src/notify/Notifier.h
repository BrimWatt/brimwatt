#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "common/Platform.h"
#include "state/SystemState.h"

namespace solar {

/** How a message leaves the board; the firmware sends it from a background task, tests record it. */
class NotifySender {
public:
    virtual ~NotifySender() = default;
    /** Posts the body without waiting for the answer (must not block the main loop). */
    virtual void sendAsync(const std::string& url, const std::string& json) = 0;
    /** Posts the body and waits (a few seconds at most); an empty string means it worked, otherwise what went wrong. */
    virtual std::string sendNow(const std::string& url, const std::string& json) = 0;
};

/**
 * Tells the owner about the few events worth a message (charging ended, solar meter lost, charger disconnected ...) by
 * posting {"text": "...", "content": "..."} to a web address they chose. Same rules as the Java app's Notifier.
 */
class Notifier {
public:
    static constexpr size_t kMaxUrlLength = 200;
    static constexpr uint64_t kRepeatMillis = 10 * 60 * 1000;

    Notifier(const Platform& platform, const SystemState& state, NotifySender& sender)
        : platform_(platform), state_(state), sender_(sender) {}

    /** True for an empty string (turn it off) or an http(s) address of a sensible length. */
    static bool isValidUrl(const std::string& value);
    static bool isWorthSending(const std::string& eventText);
    /** Only the scheme and host, since the rest of the address is usually a secret. Empty if not set. */
    static std::string maskedHost(const std::string& url);

    /** Call for every new activity line. */
    void onEvent(const std::string& text);
    /** Empty on success, otherwise what went wrong. */
    std::string sendTest();

private:
    static std::string body(const std::string& text);
    static std::string escape(const std::string& text);

    const Platform& platform_;
    const SystemState& state_;
    NotifySender& sender_;
    std::map<std::string, uint64_t> lastSent_;
};

}  // namespace solar
