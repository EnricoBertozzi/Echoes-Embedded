#include "application/service/ConfigurationMode.hpp"
#include <algorithm>

namespace {

// How often the association and the portal idle time are re-evaluated.
constexpr uint32_t POLL_INTERVAL_MS = 1000;
// Portal idle time after which an associated client no longer counts as
// configuration mode.
constexpr uint32_t PORTAL_IDLE_MS = 60000;

} // namespace

ConfigurationMode::~ConfigurationMode() {
  if (task != nullptr) {
    vTaskDelete(task);
    task = nullptr;
  }
}

void ConfigurationMode::begin() {
  if (task == nullptr) {
    xTaskCreate(taskEntry, "config-mode", 3072, this, 2, &task);
  }
}

void ConfigurationMode::registerObserver(ConfigurationModeObserver *observer) {
  std::lock_guard<std::mutex> lock(observersMutex);
  observers.push_back(observer);
}

void ConfigurationMode::unregisterObserver(
    ConfigurationModeObserver *observer) {
  std::lock_guard<std::mutex> lock(observersMutex);
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

void ConfigurationMode::taskEntry(void *param) {
  static_cast<ConfigurationMode *>(param)->run();
}

void ConfigurationMode::run() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

    const bool nowActive =
        wifiAP.countConnect() > 0 && activity.idleMs() < PORTAL_IDLE_MS;
    if (nowActive == active.load()) {
      continue;
    }
    active.store(nowActive);

    std::vector<ConfigurationModeObserver *> currentObservers;
    {
      std::lock_guard<std::mutex> lock(observersMutex);
      currentObservers = observers;
    }
    for (auto *observer : currentObservers) {
      observer->onConfigurationModeChanged(nowActive);
    }
  }
}
