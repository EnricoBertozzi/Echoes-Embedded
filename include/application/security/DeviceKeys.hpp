#pragma once

#include <string>

class DeviceKeys {
public:
  virtual ~DeviceKeys() = default;

  virtual bool load() = 0;
  virtual bool create() = 0;
  virtual bool store() = 0;
  virtual void reset() = 0;
  virtual bool isLoaded() const = 0;
  virtual std::string publicKeyPem() = 0;
  virtual bool sign(const std::string &message, std::string &der) = 0;
};
