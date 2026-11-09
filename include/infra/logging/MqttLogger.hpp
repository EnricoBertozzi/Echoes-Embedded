#pragma once

#include "application/security/MqttObserver.hpp"

class MqttLogger : public MqttObserver {
public:
  void onConnected() override;
  void onError(const std::string &reason) override;
};
