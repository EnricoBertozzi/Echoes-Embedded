#pragma once

class WifiAPObserver {
public:
  ~WifiAPObserver() = default;

  virtual void onConnect() {}
  virtual void onDisconnect() {}
};
