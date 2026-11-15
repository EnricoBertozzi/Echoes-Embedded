#include "infra/hardware/WifiSTAProxy.hpp"

WifiSTAProxy::WifiSTAProxy(WifiSTA &sta) : sta(sta) {
  queue = xQueueCreate(8, sizeof(Command));
  if (queue == nullptr) {
    return;
  }
  if (xTaskCreate(taskEntry, "wifi-sta-proxy", 4096, this, 2, &task) != pdPASS) {
    vQueueDelete(queue);
    queue = nullptr;
    task = nullptr;
  }
}

WifiSTAProxy::~WifiSTAProxy() {
  if (task != nullptr) {
    vTaskDelete(task);
    task = nullptr;
  }
  if (queue != nullptr) {
    Command command{};
    while (xQueueReceive(queue, &command, 0) == pdTRUE) {
      delete command.connectDto;
    }
    vQueueDelete(queue);
    queue = nullptr;
  }
}

void WifiSTAProxy::setup() { sta.setup(); }

void WifiSTAProxy::connect(const WifiConnectDto &connectDto) {
  enqueue(CommandType::Connect, new WifiConnectDto(connectDto));
}

void WifiSTAProxy::disconnect() { enqueue(CommandType::Disconnect); }

bool WifiSTAProxy::isConnected() { return sta.isConnected(); }

bool WifiSTAProxy::isScanning() { return sta.isScanning(); }

void WifiSTAProxy::startScan() { enqueue(CommandType::StartScan); }

void WifiSTAProxy::stopScan() { enqueue(CommandType::StopScan); }

void WifiSTAProxy::registerObserver(WifiSTAObserver *observer) {
  sta.registerObserver(observer);
}

void WifiSTAProxy::unregisterObserver(WifiSTAObserver *observer) {
  sta.unregisterObserver(observer);
}

void WifiSTAProxy::taskEntry(void *param) {
  auto *proxy = static_cast<WifiSTAProxy *>(param);
  Command command{};
  while (true) {
    if (xQueueReceive(proxy->queue, &command, portMAX_DELAY) == pdTRUE) {
      proxy->execute(command);
    }
  }
}

bool WifiSTAProxy::enqueue(CommandType type, WifiConnectDto *connectDto) {
  if (queue == nullptr) {
    delete connectDto;
    return false;
  }

  Command command{type, connectDto};
  if (xQueueSend(queue, &command, 0) != pdTRUE) {
    delete connectDto;
    return false;
  }
  return true;
}

void WifiSTAProxy::execute(const Command &command) {
  switch (command.type) {
  case CommandType::Connect:
    if (command.connectDto != nullptr) {
      sta.connect(*command.connectDto);
      delete command.connectDto;
    }
    break;
  case CommandType::Disconnect:
    sta.disconnect();
    break;
  case CommandType::StartScan:
    sta.startScan();
    break;
  case CommandType::StopScan:
    sta.stopScan();
    break;
  }
}
