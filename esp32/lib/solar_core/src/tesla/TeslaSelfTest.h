#pragma once

#include <string>

#include "tesla/TeslaCrypto.h"

namespace solar {
namespace tesla {

/**
 * Checks a Crypto implementation (and the session code on top of it) against the example values in Tesla's published protocol
 * description: key agreement, the session-info check, the signed-data prefix and one encrypted command. Returns true when all
 * pass; otherwise `report` lists what failed. No car, no key of ours. The host tests run it with OpenSSL, the `tesla selftest`
 * console command runs it on the board with mbedTLS.
 */
bool runKnownAnswerTests(Crypto& crypto, std::string& report);

}  // namespace tesla
}  // namespace solar
