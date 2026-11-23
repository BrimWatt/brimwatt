#pragma once

#include "notify/Notifier.h"

namespace device {

/**
 * Posts notifications over Wi-Fi. Events go out from a short background task so the main loop never waits; the test
 * button waits for the answer (at most about 6 seconds). Secure (https) addresses are not certificate-checked, because
 * the board holds no certificate list; the message is a short status line, never a password.
 */
class EspNotifySender : public solar::NotifySender {
public:
    void sendAsync(const std::string& url, const std::string& json) override;
    std::string sendNow(const std::string& url, const std::string& json) override;
};

}  // namespace device
