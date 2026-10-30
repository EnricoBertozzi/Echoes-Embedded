#include "infra/hardware/EspWifiSTA.hpp"
#include <WiFi.h>
#include <vector>

EspWifiSTA::EspWifiSTA() {
  xTaskCreate(
      [](void *param) {
        auto *wifiSTA = static_cast<EspWifiSTA *>(param);
        while (true) {
          if (!wifiSTA->scanning) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
          }
          wifiSTA->processScan();
          vTaskDelay(pdMS_TO_TICKS(15000));
        }
      },
      "scan-task", 4096, this, 1, &scanTask);
}

EspWifiSTA::~EspWifiSTA() {
  disconnect();
  if (scanTask != nullptr) {
    vTaskDelete(scanTask);
    scanTask = nullptr;
  }
}

void EspWifiSTA::setup() { WiFi.mode(WIFI_AP_STA); }

void EspWifiSTA::connect(const WifiConnectDto &connectDto) {}

void EspWifiSTA::disconnect() { WiFi.disconnect(); }

bool EspWifiSTA::isConnected() { return WiFi.isConnected(); }

bool EspWifiSTA::isScanning() { return scanning; }

void EspWifiSTA::startScan() {
  if (scanning) {
    return;
  }

  WiFi.scanNetworks(true, true);
  scanning = true;
}

void EspWifiSTA::stopScan() {
  if (!scanning) {
    return;
  }
  WiFi.scanDelete();
  scanning = false;
}

void EspWifiSTA::registerObserver(WifiSTAObserver *observer) {
  observers.push_back(observer);
}

void EspWifiSTA::unregisterObserver(WifiSTAObserver *observer) {
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

void EspWifiSTA::processScan() {
  if (!scanning) {
    return;
  }

  int n = WiFi.scanComplete();
  if (n <= 0) {
    return;
  }
  std::vector<WifiScanDto> networks;
  for (int i = 0; i < n; i++) {
    auto net = processScanItem(i);
    if (net.ssid == statusNetwork.ssid) {
      continue;
    }
    networks.push_back(net);
  }
  for (auto &o : observers) {
    o->onScan(networks);
  }
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
}

WifiScanDto EspWifiSTA::processScanItem(int i) {
  WifiScanDto net;
  net.ssid = std::string(WiFi.SSID(i).c_str());
  net.secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  net.status = WifiStatus::WIFI_NONE;
  net.rssi = WiFi.RSSI(i);
  return net;
}
