#pragma once

#include "application/hardware/WifiAPObserver.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/security/DeviceKeys.hpp"
#include "application/security/MqttClient.hpp"
#include "application/service/Authenticator.hpp"
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

class DeviceRegister : public WifiSTAObserver, public WifiAPObserver {
public:
  DeviceRegister(DeviceKeys &keys, MqttClient &mqtt, Authenticator &auth,
                 TimeSync &timeSync, std::string mac);
  ~DeviceRegister() override;

  void begin();
  void registerObserver(DeviceRegisterObserver *observer);
  void unregisterObserver(DeviceRegisterObserver *observer);

  // WifiSTAObserver
  void onConnect(const WifiScanDto &connectDto) override;
  void onDisconnect(const WifiScanDto &connectDto) override;
  void onError(const WifiScanDto &connectDto) override;

  // WifiAPObserver
  void onConnect() override;
  void onDisconnect() override;

private:
  static void taskEntry(void *param);
  void run();
  // Kicks the round when out of configuration mode.
  void evaluateGate();
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
  const std::string mac;

  // Written only from the Wi-Fi event task, read by evaluateGate().
  bool staConnected{false};
  uint8_t apClients{0};

  std::vector<DeviceRegisterObserver *> observers;
  TaskHandle_t task{nullptr};
  SemaphoreHandle_t ackSem{nullptr};
  std::mutex ackMutex;
  std::string ackPayload;
  std::atomic<bool> waiting{false};
  bool tokenLogged{false};
};
