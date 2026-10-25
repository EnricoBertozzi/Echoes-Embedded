#pragma once

#include <string>

class Authenticator {
public:
  virtual ~Authenticator() = default;

  virtual std::string authenticate(std::string password);
};
