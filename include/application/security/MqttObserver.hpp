#pragma once

#include <string>

class MqttObserver {
public:
  virtual ~MqttObserver() = default;

  virtual void onConnected() {}
  virtual void onError(const std::string &reason) {}
};
