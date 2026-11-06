#pragma once

class WifiAPObserver {
public:
  virtual ~WifiAPObserver() = default;

  virtual void onConnect() {}
  virtual void onDisconnect() {}
};
