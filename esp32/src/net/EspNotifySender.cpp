#include "net/EspNotifySender.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "common/Log.h"

namespace device {

namespace {

volatile int running = 0;   // background sends in flight; a few at most, extra ones are dropped

std::string post(const std::string& url, const std::string& json, int timeoutMs) {
    if (WiFi.status() != WL_CONNECTED) return "not connected to Wi-Fi";
    const bool secure = url.rfind("https://", 0) == 0;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    secureClient.setInsecure();
    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setConnectTimeout(timeoutMs);
    bool begun = secure ? http.begin(secureClient, url.c_str()) : http.begin(plainClient, url.c_str());
    if (!begun) return "the address could not be used";
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(json.c_str());
    http.end();
    if (code < 0) return std::string("request failed: ") + HTTPClient::errorToString(code).c_str();
    if (code >= 300) return "the server answered " + std::to_string(code);
    return "";
}

struct Job {
    std::string url, json;
};

void run(void* arg) {
    Job* job = static_cast<Job*>(arg);
    std::string problem = post(job->url, job->json, 6000);
    if (!problem.empty()) LOG_WARN("Could not send the notification: %s", problem.c_str());
    delete job;
    running = running - 1;
    vTaskDelete(nullptr);
}

}  // namespace

void EspNotifySender::sendAsync(const std::string& url, const std::string& json) {
    if (running >= 2) return;
    running = running + 1;
    Job* job = new Job{url, json};
    if (xTaskCreate(&run, "notify", 10240, job, 1, nullptr) != pdPASS) {
        delete job;
        running = running - 1;
    }
}

std::string EspNotifySender::sendNow(const std::string& url, const std::string& json) { return post(url, json, 6000); }

}  // namespace device
