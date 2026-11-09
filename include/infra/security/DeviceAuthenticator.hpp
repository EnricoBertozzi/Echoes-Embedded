#pragma once

#include "application/security/DeviceKeys.hpp"
#include "application/service/Authenticator.hpp"
#include <string>

class DeviceAuthenticator : public Authenticator {
public:
  DeviceAuthenticator(DeviceKeys &keys, std::string mac);

  std::string issueToken() override;

private:
  DeviceKeys &keys;
  const std::string mac;
};
