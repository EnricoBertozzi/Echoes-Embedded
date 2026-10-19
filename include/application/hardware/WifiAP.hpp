#pragma once

class WifiAP {
public:
  virtual ~WifiAP() = default;

  virtual void setup() = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
};
