#include "application/service/WifiService.hpp"
#include "Config.h"
#include <algorithm>

#ifndef WIFI_RECONNECT_INTERVAL_MS
#define WIFI_RECONNECT_INTERVAL_MS 30000
#endif

namespace {
// EspWifiSTA::connect() blocks for up to ~10 s. If neither onConnect nor
// onError arrives (for example, if the proxy queue dropped the command),
// allow a new attempt after this guard instead of waiting forever.
constexpr uint32_t ATTEMPT_TIMEOUT_MS = 15000;
} // namespace

WifiService::~WifiService() { stop(); }

void WifiService::start() {
  if (started) {
    return;
  }
  started = true;

  sta.registerObserver(this);
  ap.start();

  WifiConnectDto dto;
  if (storage.load(dto)) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      saved = dto;
      hasSaved = true;
      attemptPending = true;
      attemptStartedTick = xTaskGetTickCount();
    }
    sta.connect(dto);
  }

  if (xTaskCreate(taskEntry, "wifi-service", 3072, this, 1, &task) != pdPASS) {
    task = nullptr;
  }
}

void WifiService::stop() {
  if (!started) {
    return;
  }
  started = false;

  if (task != nullptr) {
    vTaskDelete(task);
    task = nullptr;
  }
  sta.unregisterObserver(this);
  ap.stop();
  if (sta.isConnected()) {
    sta.disconnect();
  }
}

void WifiService::connect(const WifiConnectDto &dto) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    pending = dto;
    hasPending = true;
    ++pendingGeneration;
    attemptPending = true;
    attemptStartedTick = xTaskGetTickCount();
  }
  sta.connect(dto);
}

void WifiService::onConnect(const WifiScanDto &connectDto) {
  WifiConnectDto toSave;
  uint32_t generation = 0;
  bool save = false;

  {
    std::lock_guard<std::mutex> lock(mutex);
    attemptPending = false;
    if (hasPending && pending.ssid == connectDto.ssid) {
      toSave = pending;
      generation = pendingGeneration;
      save = true;
    }
  }

  if (!save) {
    return;
  }

  // Never block the Wi-Fi event task while holding the service mutex.
  if (!storage.save(toSave)) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    if (hasPending && pendingGeneration == generation) {
      hasPending = false;
    }
    saved = toSave;
    hasSaved = true;
  }
}

void WifiService::onError(const WifiScanDto & /*connectDto*/) {
  std::lock_guard<std::mutex> lock(mutex);
  attemptPending = false;
  hasPending = false;
}

void WifiService::onDisconnect(const WifiScanDto & /*connectDto*/) {
  std::lock_guard<std::mutex> lock(mutex);
  attemptPending = false;
  hasPending = false;
}

void WifiService::onScan(const std::vector<WifiScanDto> &scanList) {
  std::lock_guard<std::mutex> lock(mutex);
  visibleNetworks.clear();
  visibleNetworks.reserve(scanList.size());
  for (const auto &network : scanList) {
    if (!network.ssid.empty()) {
      visibleNetworks.push_back(network.ssid);
    }
  }
}

void WifiService::taskEntry(void *param) {
  static_cast<WifiService *>(param)->run();
}

void WifiService::run() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_MS));
    tryReconnect();
  }
}

void WifiService::tryReconnect() {
  if (sta.isConnected()) {
    return;
  }

  WifiConnectDto dto;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!hasSaved) {
      return;
    }

    const TickType_t now = xTaskGetTickCount();
    if (attemptPending) {
      if ((now - attemptStartedTick) < pdMS_TO_TICKS(ATTEMPT_TIMEOUT_MS)) {
        return;
      }
      // The attempt never produced a result. Abandon any credentials that
      // were waiting for that connection and fall back to the saved ones.
      attemptPending = false;
      hasPending = false;
    }

    if (!networkVisibleLocked(saved.ssid)) {
      return;
    }

    dto = saved;
    attemptPending = true;
    attemptStartedTick = now;
  }

  // A connection may have completed between the check above and now.
  if (sta.isConnected()) {
    std::lock_guard<std::mutex> lock(mutex);
    attemptPending = false;
    return;
  }

  sta.connect(dto);
}

bool WifiService::networkVisibleLocked(const std::string &ssid) const {
  return std::find(visibleNetworks.begin(), visibleNetworks.end(), ssid) !=
         visibleNetworks.end();
}
