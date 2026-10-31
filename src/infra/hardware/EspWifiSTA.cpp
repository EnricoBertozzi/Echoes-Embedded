#include "infra/hardware/EspWifiSTA.hpp"
#include <vector>

EspWifiSTA::EspWifiSTA() {
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
  statusNetwork.ssid = connectDto.ssid;
  statusNetwork.status = WifiStatus::WIFI_CONNECTING;
  statusNetwork.secure = connectDto.password != "";
  if (connectDto.isDHCP())
    connectDHCP(connectDto);
  else
    connectStatic(connectDto);
}

void EspWifiSTA::connectDHCP(const WifiConnectDto &connectCmd) {
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
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
  while (!isConnected() && tries < 25) {
    vTaskDelay(pdMS_TO_TICKS(200));
    tries++;
  }
  if (isConnected()) {
    vTaskDelay(pdMS_TO_TICKS(200));
  } else {
    statusNetwork.status = WifiStatus::WIFI_ERROR;
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
  if (n == WIFI_SCAN_RUNNING || n == WIFI_SCAN_FAILED) {
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
    for (auto &o : observers) {
      o->onDisconnect(statusNetwork);
    }
    statusNetwork.status = WifiStatus::WIFI_NONE;
    statusNetwork.ssid = "";
    statusNetwork.secure = false;
    break;
  default:
    break;
  }
}
