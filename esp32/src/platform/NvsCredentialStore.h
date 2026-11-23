#pragma once

#include "setup/SetupLogic.h"

namespace device {

/**
 * Keeps what was entered on the setup page in the flash's non-volatile storage (its own "net" area, apart from the
 * charging settings). Values saved here win over secrets.h, field by field. The flash is not encrypted, so these are
 * as readable to someone holding the board as the values compiled into the firmware are.
 */
class NvsCredentialStore : public solar::setup::CredentialStore {
public:
    solar::setup::Credentials load() override;
    void save(const solar::setup::Credentials& credentials) override;
    solar::setup::TrialState loadTrial() override;
    void saveTrial(const solar::setup::TrialState& trial) override;
    void clear() override;
};

}  // namespace device
