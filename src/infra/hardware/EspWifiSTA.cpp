#include "infra/hardware/EspWifiSTA.hpp"
#include <algorithm>
#include <esp_wifi.h>
#include <vector>

EspWifiSTA::EspWifiSTA() {
  xTaskCreate(
      [](void *param) {
        auto *wifiSTA = static_cast<EspWifiSTA *>(param);
        while (true) {
          if (wifiSTA->scanning) {
            wifiSTA->processScan();
          }
          vTaskDelay(pdMS_TO_TICKS(250));
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
  stopScan();

  WifiScanDto status;
  {
    std::lock_guard<std::mutex> lock(statusMutex);
    statusNetwork.ssid = connectDto.ssid;
    statusNetwork.status = WifiStatus::WIFI_CONNECTING;
    statusNetwork.secure = !connectDto.password.empty();
    statusNetwork.rssi = 0;
    status = statusNetwork;
  }
  for (auto *o : lockedObservers()) {
    o->onConnecting(status);
  }

  if (connectDto.isDHCP())
    connectDHCP(connectDto);
  else
    connectStatic(connectDto);
}

void EspWifiSTA::connectDHCP(const WifiConnectDto &connectCmd) {
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
    const WifiScanDto status = setErrorStatus();
    for (auto *o : lockedObservers()) {
      o->onError(status);
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
    const WifiScanDto status = setErrorStatus();
    for (auto *o : lockedObservers()) {
      o->onError(status);
    }
  }
}

void EspWifiSTA::disconnect() { WiFi.disconnect(); }

bool EspWifiSTA::isConnected() { return WiFi.isConnected(); }

bool EspWifiSTA::isScanning() { return scanning; }

void EspWifiSTA::startScan() {
  // The scan task picks this up and runs blocking scans while it is set.
  scanning = true;
}

void EspWifiSTA::stopScan() {
  if (!scanning.exchange(false)) {
    return;
  }
  // Aborts a blocking scan in progress, if any; the scan task discards
  // the partial result because `scanning` is already false.
  esp_wifi_scan_stop();
  std::lock_guard<std::mutex> lock(scanMutex);
  WiFi.scanDelete();
}

void EspWifiSTA::registerObserver(WifiSTAObserver *observer) {
  std::lock_guard<std::mutex> lock(observersMutex);
  observers.push_back(observer);
}

void EspWifiSTA::unregisterObserver(WifiSTAObserver *observer) {
  std::lock_guard<std::mutex> lock(observersMutex);
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

std::vector<WifiSTAObserver *> EspWifiSTA::lockedObservers() {
  std::lock_guard<std::mutex> lock(observersMutex);
  return observers;
}

WifiScanDto EspWifiSTA::snapshotStatus() {
  std::lock_guard<std::mutex> lock(statusMutex);
  return statusNetwork;
}

WifiScanDto EspWifiSTA::setErrorStatus() {
  std::lock_guard<std::mutex> lock(statusMutex);
  statusNetwork.status = WifiStatus::WIFI_ERROR;
  return statusNetwork;
}

void EspWifiSTA::processScan() {
  if (!scanning) {
    return;
  }

  // Blocking all-channel scan: guarantees a complete result list. The old
  // async scan + poll flow could time out (6s in the Arduino core) and
  // restart the scan mid-flight, which made the Wi-Fi driver report only
  // the partial results of the overridden scan (one or two networks per
  // event). This task exists precisely to block here.
  const int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    Serial.printf("[Scan] failed (%d), retrying\n", n);
    std::lock_guard<std::mutex> lock(scanMutex);
    WiFi.scanDelete();
    return;
  }

  std::vector<WifiScanDto> networks;
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    // stopScan() may have run while we were waiting for the scan.
    if (!scanning) {
      WiFi.scanDelete();
      return;
    }

    std::string connectedSsid;
    {
      std::lock_guard<std::mutex> statusLock(statusMutex);
      connectedSsid = statusNetwork.ssid;
    }

    for (int i = 0; i < n; i++) {
      auto net = processScanItem(i);
      // Hide the network being connected/connected to, but keep hidden
      // networks (empty ssid) in the list when nothing is being connected.
      if (!connectedSsid.empty() && net.ssid == connectedSsid) {
        continue;
      }
      networks.push_back(std::move(net));
    }

    WiFi.scanDelete();
  }

  for (auto *o : lockedObservers()) {
    o->onScan(networks);
  }
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
  case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
    WifiScanDto status;
    {
      std::lock_guard<std::mutex> lock(statusMutex);
      statusNetwork.status = WifiStatus::WIFI_CONNECTED;
      statusNetwork.rssi = WiFi.RSSI();
      status = statusNetwork;
    }
    for (auto *o : lockedObservers()) {
      o->onConnect(status);
    }
    break;
  }
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
    WifiScanDto status;
    {
      std::lock_guard<std::mutex> lock(statusMutex);
      if (statusNetwork.status == WifiStatus::WIFI_CONNECTING) {
        // The connect() loop reports the failure itself.
        break;
      }
      status = statusNetwork;
      statusNetwork.status = WifiStatus::WIFI_NONE;
      statusNetwork.ssid.clear();
      statusNetwork.secure = false;
      statusNetwork.rssi = 0;
    }
    if (status.status == WifiStatus::WIFI_CONNECTED) {
      for (auto *o : lockedObservers()) {
        o->onDisconnect(status);
      }
    }
    break;
  }
  default:
    break;
  }
}
