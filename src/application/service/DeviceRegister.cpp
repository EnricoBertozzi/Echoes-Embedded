#include "application/service/DeviceRegister.hpp"
#include "application/security/JwtBuilder.hpp"
#include <ArduinoJson.h>
#include <cctype>
#include <algorithm>
#include <time.h>

// Injected by extra/getenv.py from .env / system environment; the fallbacks
// follow the platform defaults (device-api.md section 2.1).
#ifndef MQTT_REGISTRATION_TOPIC
#define MQTT_REGISTRATION_TOPIC "register"
#endif
#ifndef MQTT_ACK_TOPIC
#define MQTT_ACK_TOPIC "register/ack/{mac}"
#endif

namespace {

constexpr char CANONICAL_PREFIX[] = "echoes-device-registration-v1";
// 2020-01-01: minimum sane clock, the JWT `exp` claim needs SNTP first.
constexpr time_t MIN_VALID_TIME = 1577836800;

std::string trimmed(std::string s) {
  const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

std::string ackTopicFor(const std::string &mac) {
  std::string topic(MQTT_ACK_TOPIC);
  const std::string placeholder = "{mac}";
  const size_t pos = topic.find(placeholder);
  if (pos != std::string::npos) {
    topic.replace(pos, placeholder.size(), mac);
  }
  return topic;
}

} // namespace

DeviceRegister::DeviceRegister(DeviceKeys &keys, MqttClient &mqtt,
                                Authenticator &auth, TimeSync &timeSync,
                                std::string mac)
    : keys(keys), mqtt(mqtt), auth(auth), timeSync(timeSync),
      mac(std::move(mac)) {}

DeviceRegister::~DeviceRegister() {
  if (task != nullptr) {
    vTaskDelete(task);
    task = nullptr;
  }
  if (ackSem != nullptr) {
    vSemaphoreDelete(ackSem);
    ackSem = nullptr;
  }
}

void DeviceRegister::begin() {
  if (ackSem == nullptr) {
    ackSem = xSemaphoreCreateBinary();
  }
  if (task == nullptr) {
    xTaskCreate(taskEntry, "device-registrar", 8192, this, 2, &task);
  }
  mqtt.onMessage([this](const std::string &topic, const std::string &payload) {
    this->onMqttMessage(topic, payload);
  });
}

void DeviceRegister::registerObserver(DeviceRegisterObserver *observer) {
  observers.push_back(observer);
}

void DeviceRegister::unregisterObserver(DeviceRegisterObserver *observer) {
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

void DeviceRegister::onConnect(const WifiScanDto & /*connectDto*/) {
  staConnected = true;
  evaluateGate();
}

void DeviceRegister::onDisconnect(const WifiScanDto & /*connectDto*/) {
  staConnected = false;
}

void DeviceRegister::onError(const WifiScanDto & /*connectDto*/) {
  staConnected = false;
}

void DeviceRegister::onConnect() {
  // Configuration mode: platform actions are deferred.
  apClients++;
}

void DeviceRegister::onDisconnect() {
  if (apClients > 0) {
    apClients--;
  }
  evaluateGate();
}

void DeviceRegister::evaluateGate() {
  if (staConnected && apClients == 0 && task != nullptr) {
    xTaskNotifyGive(task);
  }
}

void DeviceRegister::taskEntry(void *param) {
  static_cast<DeviceRegister *>(param)->run();
}

void DeviceRegister::run() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Out of configuration mode: the radio is quiet for the whole round —
    // clock sync, TLS registration and the access token.
    for (auto *o : observers) {
      o->onRegistering();
    }
    timeSync.sync();
    registerDevice();

    if (keys.isLoaded()) {
      logAccessToken();
    }
  }
}

void DeviceRegister::registerDevice() {
  // Prefer the pre-provisioned/stored key; generate one only when absent.
  const bool generated = !keys.load();
  if (generated) {
    for (auto *o : observers) {
      o->onGeneratingKey(mac);
    }
    if (!keys.create()) {
      for (auto *o : observers) {
        o->onKeyGenerationFailed();
      }
      return;
    }
  } else {
    for (auto *o : observers) {
      o->onKeyLoaded();
    }
  }

  const std::string publicKey = trimmed(keys.publicKeyPem());

  if (!mqtt.connect(20000)) {
    retryLater(generated, "MQTT broker unreachable");
    return;
  }

  // Canonical message signed by the device (device-api.md section 2.3).
  const std::string message =
      std::string(CANONICAL_PREFIX) + "\n" + mac + "\n" + publicKey;
  std::string der;
  if (!keys.sign(message, der)) {
    retryLater(generated, "failed to sign the registration message");
    return;
  }

  JsonDocument doc;
  doc["mac"] = mac;
  doc["public_key"] = publicKey;
  // base64url of the DER signature is accepted (section 2.4).
  doc["signature"] = JwtBuilder::base64Url(der);
  std::string payload;
  serializeJson(doc, payload);

  {
    std::lock_guard<std::mutex> lock(ackMutex);
    ackPayload.clear();
    waiting = true;
  }
  xSemaphoreTake(ackSem, 0); // drain a stale ack from a previous attempt

  if (!mqtt.subscribe(ackTopicFor(mac)) ||
      !mqtt.publish(MQTT_REGISTRATION_TOPIC, payload)) {
    retryLater(generated, "MQTT subscribe/publish failed");
    return;
  }

  std::string ack;
  if (!waitForAck(ack, 15000)) {
    retryLater(generated, "no registration ack (is the platform online?)");
    return;
  }

  JsonDocument ackDoc;
  if (deserializeJson(ackDoc, ack) != DeserializationError::Ok) {
    retryLater(generated, "malformed registration ack");
    return;
  }

  const std::string status = ackDoc["status"] | "";
  const std::string deviceId = ackDoc["deviceId"] | "";

  if (status == "REGISTERED" || status == "ALREADY_REGISTERED") {
    if (generated && !keys.store()) {
      // Without the file the next boot would register a NEW key, and the
      // platform never overwrites a MAC: keep the key in RAM for this boot.
      for (auto *o : observers) {
        o->onKeySaveFailed();
      }
    } else if (generated) {
      for (auto *o : observers) {
        o->onKeySaved();
      }
    }
    for (auto *o : observers) {
      o->onRegistered(status, deviceId);
    }
    mqtt.disconnect();
    return;
  }

  for (auto *o : observers) {
    o->onRejected(ackDoc["message"] | "", !generated);
  }
  // A generated key was never stored; a pre-provisioned one stays on flash
  // (only the in-memory copy is dropped, it is reloaded on the next wake).
  failRegistration();
}

void DeviceRegister::failRegistration() {
  mqtt.disconnect();
  keys.reset();
}

void DeviceRegister::retryLater(bool generated, const char *reason) {
  for (auto *o : observers) {
    o->onRetryLater(reason, !generated);
  }
  if (generated) {
    // Nothing was confirmed and nothing was stored: discard and retry.
    failRegistration();
  } else {
    // The pre-provisioned key is presumably already known by the platform;
    // keep it usable and let a later connection confirm.
    mqtt.disconnect();
  }
}

bool DeviceRegister::waitForAck(std::string &out, uint32_t timeoutMs) {
  if (xSemaphoreTake(ackSem, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
    std::lock_guard<std::mutex> lock(ackMutex);
    waiting = false;
    return false;
  }
  std::lock_guard<std::mutex> lock(ackMutex);
  waiting = false;
  out = ackPayload;
  ackPayload.clear();
  return true;
}

void DeviceRegister::onMqttMessage(const std::string &topic,
                                   const std::string &payload) {
  if (!waiting || topic != ackTopicFor(mac)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(ackMutex);
    ackPayload = payload;
  }
  xSemaphoreGive(ackSem);
}

void DeviceRegister::logAccessToken() {
  if (tokenLogged) {
    return;
  }
  // The JWT `exp` claim needs a synced clock; give SNTP a moment.
  for (int i = 0; i < 100 && time(nullptr) < MIN_VALID_TIME; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  const std::string token = auth.issueToken();
  if (token.empty()) {
    for (auto *o : observers) {
      o->onClockNotSynced();
    }
    return;
  }
  tokenLogged = true;
  for (auto *o : observers) {
    o->onTokenIssued(token);
  }
}
