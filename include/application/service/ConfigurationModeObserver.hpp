#pragma once

class ConfigurationModeObserver {
public:
  virtual ~ConfigurationModeObserver() = default;

  virtual void onConfigurationModeChanged(bool active) {}
};
