#pragma once

#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/security/DeviceKeys.hpp"
#include "application/security/MqttClient.hpp"
#include "application/service/Authenticator.hpp"
#include "application/service/ConfigurationMode.hpp"
#include "application/service/ConfigurationModeObserver.hpp"
#include "application/service/DeviceRegisterObserver.hpp"
#include "application/service/TimeSync.hpp"
#include <atomic>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mutex>
#include <string>
#include <vector>

class DeviceRegister : public WifiSTAObserver,
                       public ConfigurationModeObserver {
public:
  DeviceRegister(DeviceKeys &keys, MqttClient &mqtt, Authenticator &auth,
                 TimeSync &timeSync, WifiSTA &wifiSTA,
                 ConfigurationMode &configurationMode, std::string mac);
  ~DeviceRegister() override;

  void begin();
  void registerObserver(DeviceRegisterObserver *observer);
  void unregisterObserver(DeviceRegisterObserver *observer);

  // WifiSTAObserver / ConfigurationModeObserver: only transitions matter,
  // the state itself comes from the ports.
  void onConnect(const WifiScanDto &connectDto) override;
  void onDisconnect(const WifiScanDto &connectDto) override;
  void onError(const WifiScanDto &connectDto) override;
  void onConfigurationModeChanged(bool active) override;

private:
  static void taskEntry(void *param);
  void run();
  // Wakes the task so it re-evaluates the gate right away.
  void wake();
  void registerDevice();
  void failRegistration();
  void retryLater(bool generated, const char *reason);
  bool waitForAck(std::string &out, uint32_t timeoutMs);
  void onMqttMessage(const std::string &topic, const std::string &payload);
  void logAccessToken();

  DeviceKeys &keys;
  MqttClient &mqtt;
  Authenticator &auth;
  TimeSync &timeSync;
  WifiSTA &wifiSTA;
  ConfigurationMode &configurationMode;
  const std::string mac;

  std::vector<DeviceRegisterObserver *> observers;
  TaskHandle_t task{nullptr};
  SemaphoreHandle_t ackSem{nullptr};
  std::mutex ackMutex;
  std::string ackPayload;
  std::atomic<bool> waiting{false};
  bool tokenLogged{false};
};
