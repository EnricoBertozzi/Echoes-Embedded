#include "infra/hardware/EspWifiAP.hpp"
#include <algorithm>
#include <esp_wifi.h>

namespace {
// How often the driver station table is compared with the announced count.
constexpr uint32_t RECONCILE_INTERVAL_MS = 1000;
} // namespace

EspWifiAP::EspWifiAP() {
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t /*info*/) {
    this->onWifiEvent(event);
  });
  xTaskCreate(
      [](void *task) {
        auto *wifiAp = static_cast<EspWifiAP *>(task);
        while (true) {
          if (wifiAp->running) {
            wifiAp->dns.processNextRequest();
            wifiAp->reconcileClients();
          }
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      },
      "dns-loop", 4096, this, 1, &dnsLoop);
}

EspWifiAP::~EspWifiAP() {
  stop();
  if (dnsLoop != nullptr) {
    vTaskDelete(dnsLoop);
    dnsLoop = nullptr;
  }
}

void EspWifiAP::start() {
  if (running) {
    return;
  }

  WiFi.softAPConfig(apIP, gateway, subnet);
  WiFi.softAP("Echoes Setup", "echoes123456");
  dns.start(DNS_PORT, "*", apIP);

  running = true;
  vTaskResume(dnsLoop);
}

void EspWifiAP::stop() {
  if (!running) {
    return;
  }

  WiFi.softAPdisconnect(true);
  dns.stop();
  running = false;
  vTaskSuspend(dnsLoop);
}

void EspWifiAP::registerObserver(WifiAPObserver *observer) {
  std::lock_guard<std::mutex> lock(observersMutex);
  observers.push_back(observer);
}

void EspWifiAP::unregisterObserver(WifiAPObserver *observer) {
  std::lock_guard<std::mutex> lock(observersMutex);
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

std::size_t EspWifiAP::countConnect() {
  // The driver station table is the source of truth; the announced count is
  // only a fallback when the driver is not up.
  wifi_sta_list_t list{};
  if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) {
    return announcedClients.load();
  }
  return list.num;
}

std::vector<WifiAPObserver *> EspWifiAP::lockedObservers() {
  std::lock_guard<std::mutex> lock(observersMutex);
  return observers;
}

void EspWifiAP::reconcileClients() {
  const uint32_t now = millis();
  if (now - lastReconcile < RECONCILE_INTERVAL_MS) {
    return;
  }
  lastReconcile = now;
  syncClients();
}

void EspWifiAP::syncClients() {
  const std::size_t actual = countConnect();
  std::lock_guard<std::mutex> lock(clientsMutex);
  if (actual == announcedClients) {
    return;
  }

  // The driver table is authoritative: announce the delta so the
  // configuration-mode gate and the scan policy always see reality, even
  // when the join/leave event was not delivered.
  const auto currentObservers = lockedObservers();
  while (announcedClients < actual) {
    announcedClients++;
    for (auto &obs : currentObservers)
      obs->onConnect();
  }
  while (announcedClients > actual) {
    announcedClients--;
    for (auto &obs : currentObservers)
      obs->onDisconnect();
  }
}

void EspWifiAP::onWifiEvent(WiFiEvent_t event) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
  case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
    // React immediately; the periodic reconciliation covers missed events.
    syncClients();
    break;
  default:
    break;
  }
}
