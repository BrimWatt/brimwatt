#pragma once

#include <functional>

#include "common/Platform.h"
#include "history/HistoryService.h"
#include "ocpp/OcppRouter.h"

namespace solar::ocpp {

/**
 * Registers the OCPP 1.6 core actions a charger initiates: BootNotification, Heartbeat, StatusNotification,
 * Authorize, StartTransaction, StopTransaction and MeterValues.
 *
 * @param onChargerChanged called after the charger's status or transaction changes
 * @param history           if given, plugging in and unplugging the car are added to the activity log
 */
void registerCoreHandlers(OcppRouter& router, Platform& platform, std::function<void(Charger&)> onChargerChanged,
                          HistoryService* history = nullptr);

}  // namespace solar::ocpp
