#pragma once

#include "application/security/MqttObserver.hpp"
#include <functional>
#include <string>

class MqttClient {
public:
  using MessageHandler =
      std::function<void(const std::string &topic, const std::string &payload)>;

  virtual ~MqttClient() = default;

  virtual void onMessage(MessageHandler handler) = 0;
  virtual bool connect(uint32_t timeoutMs) = 0;
  virtual void disconnect() = 0;
  virtual bool subscribe(const std::string &topic) = 0;
  virtual bool publish(const std::string &topic,
                       const std::string &payload) = 0;

  virtual void registerObserver(MqttObserver *observer) = 0;
  virtual void unregisterObserver(MqttObserver *observer) = 0;
};
