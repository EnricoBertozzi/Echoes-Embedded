#pragma once

#include "application/hardware/WifiSTA.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class WifiSTAProxy : public WifiSTA {
public:
  explicit WifiSTAProxy(WifiSTA &sta);
  ~WifiSTAProxy() override;

  void setup() override;
  void connect(const WifiConnectDto &connectDto) override;
  void disconnect() override;
  bool isConnected() override;
  bool isScanning() override;
  void startScan() override;
  void stopScan() override;

  void registerObserver(WifiSTAObserver *observer) override;
  void unregisterObserver(WifiSTAObserver *observer) override;

private:
  enum class CommandType { Connect, Disconnect, StartScan, StopScan };

  struct Command {
    CommandType type;
    WifiConnectDto *connectDto;
  };

  WifiSTA &sta;
  QueueHandle_t queue{nullptr};
  TaskHandle_t task{nullptr};

  static void taskEntry(void *param);
  bool enqueue(CommandType type, WifiConnectDto *connectDto = nullptr);
  void execute(const Command &command);
};
