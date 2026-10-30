#include "infra/hardware/EspWifiAP.hpp"
#include <algorithm>

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
          }
          vTaskDelay(pdTICKS_TO_MS(10));
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
  observers.push_back(observer);
}

void EspWifiAP::unregisterObserver(WifiAPObserver *observer) {
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

void EspWifiAP::onWifiEvent(WiFiEvent_t event) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    countConnect++;
    for (auto &obs : observers)
      obs->onConnect();
    break;

  case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
    if (countConnect > 0) {
      countConnect--;
    }
    for (auto &obs : observers)
      obs->onDisconnect();
    break;
  default:
    break;
  }
}
