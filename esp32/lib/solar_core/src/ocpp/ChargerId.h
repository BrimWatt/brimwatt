#pragma once

#include <string>

#include "common/Config.h"

namespace solar::ocpp {

/**
 * Works out which charger is connecting from the WebSocket URL: /pair/<id> (what owners enter is just /pair; the wallbox
 * adds its own id), the older /ocpp/wallbox/<id>, or ?chargerId=<id>, or the default id when none is given.
 */
inline std::string chargerIdFromUrl(const std::string& url) {
    std::string path = url;
    std::string query;
    size_t q = url.find('?');
    if (q != std::string::npos) {
        path = url.substr(0, q);
        query = url.substr(q + 1);
    }

    for (const char* prefix : {"/pair/", "/ocpp/wallbox/"}) {
        const std::string p = prefix;
        if (path.compare(0, p.size(), p) == 0) {
            std::string id = path.substr(p.size());
            while (!id.empty() && id.back() == '/') id.pop_back();
            if (!id.empty()) return id;
        }
    }

    const std::string key = "chargerId=";
    size_t k = query.find(key);
    if (k != std::string::npos && (k == 0 || query[k - 1] == '&')) {
        std::string id = query.substr(k + key.size());
        size_t end = id.find('&');
        if (end != std::string::npos) id = id.substr(0, end);
        if (!id.empty()) return id;
    }
    return config::kDefaultChargerId;
}

}  // namespace solar::ocpp
