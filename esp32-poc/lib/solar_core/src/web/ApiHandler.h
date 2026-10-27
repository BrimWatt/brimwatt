#pragma once

#include <map>
#include <string>

#include "car/CarControl.h"
#include "car/ChargeRouter.h"
#include "notify/Notifier.h"
#include "common/Platform.h"
#include "control/SolarController.h"
#include "daylight/DaylightSchedule.h"
#include "web/Diagnostics.h"
#include "history/HistoryService.h"
#include "wallbox/ChargeDevice.h"

namespace solar {

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/plain";
    std::string body;
    std::string downloadName;   // if set, the browser saves the reply under this name
};

/**
 * The dashboard's REST API, independent of any web server: the firmware passes in the method, path and query
 * parameters and sends back what this returns. The endpoints and their messages are the same as the Java app's:
 *
 *   GET  /api/telemetry
 *   POST /api/mode?mode=ECO|MAX_SOLAR|FAST[&amps=6-40]
 *   POST /api/allowance?watts=0-1000
 *   POST /api/daylight?enabled=true|false&startMin=0-180&endMin=0-180   (each parameter optional)
 *   POST /api/pause?enabled=true|false      the kill switch
 *   POST /api/rate?cents=0-200               the electricity price used for the savings
 *   POST /api/maxamps?amps=6-40              the most current any mode will ask for (the house circuit's limit)
 *   GET  /api/history[?daysAgo=N]            a day's power by 5 minutes (hourly after 30 days) and the energy totals
 *   GET  /api/sessions?n=10[&offset=0]       the charging sessions, newest first (one page), the total, and the one running now
 *   GET  /api/events?n=5[&offset=0]          the activity log, newest first (one page, n up to 20), and the total
 *   GET  /api/days?n=30                      daily energy totals for the last n days (up to 365)
 *   GET  /api/diagnostics                    restarts and crashes, uptime, memory, Wi-Fi signal (404 if the platform has none)
 *   GET  /api/export.csv                     every day's energy totals as a spreadsheet file
 *   POST /api/car/pair  (vin in the body)    adds this controller's own key to the owner's Tesla (the owner taps the key card)
 *   POST /api/car/refresh                    reads the car now (read only)
 *   POST /api/car/watch?enabled=true|false   read the car every few minutes (read only)
 *   POST /api/car/test?action=start|stop|amps[&amps=6-40]   test commands: these CHANGE the car
 *   POST /api/car/through?mode=charger|car[&confirm=yes]   who is in command of the charge (the car needs confirm=yes and a paired car)
 *   POST /api/car/policy?onReturn=keep|tell|stop   what to do when the car is back after 10+ minutes out of reach
 *   POST /api/car/wake                       wakes a sleeping car now (a test command; the car stays awake a while and uses a little battery)
 *   POST /api/car/allowwake?enabled=true|false   may WattSync wake a sleeping car when charging is wanted (off by default)
 *   POST /api/car/cancel                     ends a pairing or test early
 *   POST /api/car/forget                     deletes our key and the car from this controller
 *   (the car's state is part of /api/telemetry as "car")
 */
class ApiHandler {
public:
    using Params = std::map<std::string, std::string>;

    ApiHandler(const Platform& platform, SolarController& controller, ChargeDevice& wallbox,
               const DaylightSchedule& schedule)
        : platform_(platform), controller_(controller), wallbox_(wallbox), schedule_(schedule) {}

    /**
     * Status 404 for anything that is not an API endpoint. `hasSafeHeader` says whether the request carried the
     * X-WattSync header; POSTs without it are refused with 403 (cross-site protection).
     */
    HttpResponse handle(const std::string& method, const std::string& path, const Params& query, bool hasSafeHeader = true);

    void setHistory(const HistoryService* history) { history_ = history; }
    /** Needed for the notification test button; without it the test reports an error. */
    void setNotifier(Notifier* notifier) { notifier_ = notifier; }
    void setDiagnostics(const DiagnosticsSource* source) { diagnostics_ = source; }
    /** The Bluetooth link to the car; without it the car endpoints answer "not supported". */
    void setCar(CarControl* car) { car_ = car; }
    /** Who is in command of the charge (wallbox or car). Without it "Control through: Car" is not offered. */
    void setRouter(ChargeRouter* router) { router_ = router; }

private:
    HttpResponse telemetry();
    HttpResponse updateMode(const Params& query);
    HttpResponse updateAllowance(const Params& query);
    HttpResponse updateDaylight(const Params& query);
    HttpResponse updatePause(const Params& query);
    HttpResponse updateRate(const Params& query);
    HttpResponse updateGoal(const Params& query);
    HttpResponse updateNotify(const Params& query);
    HttpResponse testNotify();
    HttpResponse updateMaxAmps(const Params& query);
    HttpResponse history(const Params& query);
    HttpResponse days(const Params& query);
    HttpResponse sessions(const Params& query);
    HttpResponse events(const Params& query);
    HttpResponse diagnostics();
    HttpResponse exportCsv();
    HttpResponse car(const std::string& path, const Params& query);

    const Platform& platform_;
    SolarController& controller_;
    ChargeDevice& wallbox_;
    const DaylightSchedule& schedule_;
    const HistoryService* history_ = nullptr;
    const DiagnosticsSource* diagnostics_ = nullptr;
    Notifier* notifier_ = nullptr;
    CarControl* car_ = nullptr;
    ChargeRouter* router_ = nullptr;
};

}  // namespace solar
