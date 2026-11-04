#include "infra/hardware/EspWifiSTA.hpp"
#include <esp_wifi.h>
#include <vector>

EspWifiSTA::EspWifiSTA() {
  connectQueue = xQueueCreate(1, sizeof(WifiConnectDto *));
  xTaskCreate(connectTaskEntry, "wifi-connect", 4096, this, 2, &connectTask);
  xTaskCreate(
      [](void *param) {
        auto *wifiSTA = static_cast<EspWifiSTA *>(param);
        while (true) {
          if (wifiSTA->scanning) {
            wifiSTA->processScan();
            vTaskDelay(pdMS_TO_TICKS(50));
          } else {
            vTaskDelay(pdMS_TO_TICKS(500));
          }
        }
      },
      "scan-task", 4096, this, 1, &scanTask);
}

EspWifiSTA::~EspWifiSTA() {
  disconnect();
  if (connectTask != nullptr) {
    vTaskDelete(connectTask);
    connectTask = nullptr;
  }
  if (connectQueue != nullptr) {
    WifiConnectDto *pending = nullptr;
    while (xQueueReceive(connectQueue, &pending, 0) == pdTRUE) {
      delete pending;
    }
    vQueueDelete(connectQueue);
    connectQueue = nullptr;
  }
  if (scanTask != nullptr) {
    vTaskDelete(scanTask);
    scanTask = nullptr;
  }
}

void EspWifiSTA::setup() {
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
    this->onWifiEvent(event, info);
  });
  WiFi.mode(WIFI_AP_STA);
}

void EspWifiSTA::connect(const WifiConnectDto &connectDto) {
  if (connecting.exchange(true)) {
    return;
  }

  statusNetwork.ssid = connectDto.ssid;
  statusNetwork.status = WifiStatus::WIFI_CONNECTING;
  statusNetwork.secure = connectDto.password != "";
  for (auto &o : observers) {
    o->onConnecting(statusNetwork);
  }

  auto *request = new WifiConnectDto(connectDto);
  if (connectQueue == nullptr ||
      xQueueSend(connectQueue, &request, 0) != pdTRUE) {
    delete request;
    connecting.store(false);
    statusNetwork.status = WifiStatus::WIFI_ERROR;
    for (auto &o : observers) {
      o->onError(statusNetwork);
    }
  }
}

void EspWifiSTA::connectTaskEntry(void *param) {
  auto *self = static_cast<EspWifiSTA *>(param);
  WifiConnectDto *request = nullptr;
  while (true) {
    if (xQueueReceive(self->connectQueue, &request, portMAX_DELAY) == pdTRUE) {
      self->performConnect(*request);
      delete request;
      self->connecting.store(false);
    }
  }
}

void EspWifiSTA::performConnect(const WifiConnectDto &connectDto) {
  stopScan();
  if (connectDto.isDHCP())
    connectDHCP(connectDto);
  else
    connectStatic(connectDto);
}

void EspWifiSTA::connectDHCP(const WifiConnectDto &connectCmd) {
  // 0.0.0.0 tells the Arduino core to stop the DHCP client, clear the static
  // address and start DHCP again. Do not use INADDR_NONE (255.255.255.255)
  // here: it leaves the DHCP client stopped and marks the interface as
  // static, so the STA associates but never receives an IP.
  WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0),
              IPAddress(0, 0, 0, 0));
  connect(connectCmd.ssid, connectCmd.password);
}

void EspWifiSTA::connectStatic(const WifiConnectDto &connectCmd) {
  IPAddress ipParsed, gatewayParsed, subnetParsed;
  IPAddress dns1(8, 8, 8, 8);
  IPAddress dns2(8, 8, 4, 4);

  auto failParseIps = !ipParsed.fromString(connectCmd.ip.c_str()) ||
                      !gatewayParsed.fromString(connectCmd.gateway.c_str()) ||
                      !subnetParsed.fromString(connectCmd.subnet.c_str());

  if (failParseIps) {
    statusNetwork.status = WifiStatus::WIFI_ERROR;
    for (auto &o : observers) {
      o->onError(statusNetwork);
    }
    return;
  }

  WiFi.config(ipParsed, gatewayParsed, subnetParsed, dns1, dns2);
  connect(connectCmd.ssid, connectCmd.password);
}

void EspWifiSTA::connect(const std::string &ssid, const std::string &pass) {
  if (isConnected()) {
    disconnect();
  }

  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.begin(ssid.c_str(), pass.c_str());

  int tries = 0;
  while (!isConnected() && tries < 50) {
    wl_status_t status = WiFi.status();
    if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    tries++;
  }
  if (isConnected()) {
    WiFi.setAutoReconnect(true);
    vTaskDelay(pdMS_TO_TICKS(200));
  } else {
    statusNetwork.status = WifiStatus::WIFI_ERROR;
    for (auto &o : observers) {
      o->onError(statusNetwork);
    }
  }
}

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
  if (!scanning.exchange(false)) {
    return;
  }
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    esp_wifi_scan_stop();
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
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    if (scanning) {
      WiFi.scanNetworks(true, true);
    }
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

void EspWifiSTA::onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    statusNetwork.status = WifiStatus::WIFI_CONNECTED;
    for (auto &o : observers) {
      o->onConnect(statusNetwork);
    }
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    // While a connection attempt is in progress the worker task owns the
    // outcome (it reports onError on timeout). Ignoring this event avoids a
    // spurious "disconnected" notification and keeps the SSID for the error.
    if (statusNetwork.status == WifiStatus::WIFI_CONNECTING) {
      break;
    }
    if (statusNetwork.status == WifiStatus::WIFI_CONNECTED) {
      for (auto &o : observers) {
        o->onDisconnect(statusNetwork);
      }
    }
    statusNetwork.status = WifiStatus::WIFI_NONE;
    statusNetwork.ssid.clear();
    statusNetwork.secure = false;
    break;
  default:
    break;
  }
}
