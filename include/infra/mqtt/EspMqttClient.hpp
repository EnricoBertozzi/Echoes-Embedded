#pragma once

#include "application/security/MqttClient.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mqtt_client.h>
#include <string>
#include <vector>

// Injected by extra/getenv.py from .env / system environment; the fallbacks
// keep the build working when a variable is not set. The client id is the
// prefix below concatenated with the device mac at runtime
// (e.g. echoes-device-aa-bb-cc-dd-ee-ff), so every device gets a unique id.
#ifndef MQTT_BROKER_URL
#define MQTT_BROKER_URL ""
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "echoes-device"
#endif
#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif
#ifndef MQTT_QOS
#define MQTT_QOS 1
#endif

class EspMqttClient : public MqttClient {
public:
  EspMqttClient(const std::string &uri, const std::string &mac);
  ~EspMqttClient() override;

  void onMessage(MessageHandler handler) override;
  bool connect(uint32_t timeoutMs) override;
  void disconnect() override;
  bool subscribe(const std::string &topic) override;
  bool publish(const std::string &topic, const std::string &payload) override;
  void registerObserver(MqttObserver *observer) override;
  void unregisterObserver(MqttObserver *observer) override;

private:
  static void onEvent(void *arg, esp_event_base_t base, int32_t id, void *data);
  void handleEvent(esp_mqtt_event_id_t id, esp_mqtt_event_handle_t event);

  const std::string uri;     // must outlive the client configuration
  const std::string clientId; // must outlive the client configuration
  esp_mqtt_client_handle_t client{nullptr};
  SemaphoreHandle_t connectedSem{nullptr};
  MessageHandler handler; // set before the first connect()
  std::vector<MqttObserver *> observers; // registered before the first connect()
  bool started{false};
};
