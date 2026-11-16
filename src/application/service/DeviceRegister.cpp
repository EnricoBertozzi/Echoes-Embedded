#include "application/service/DeviceRegister.hpp"
#include "Config.h"
#include "application/security/JwtBuilder.hpp"
#include <ArduinoJson.h>
#include <cctype>
#include <algorithm>
#include <time.h>

#ifndef MQTT_RECONNECT_INTERVAL_MS
#define MQTT_RECONNECT_INTERVAL_MS 30000
#endif

// Injected by extra/getenv.py from .env / system environment; the fallbacks
// follow the platform defaults (device-api.md section 2.1).
#ifndef MQTT_REGISTRATION_TOPIC
#define MQTT_REGISTRATION_TOPIC "register"
#endif
#ifndef MQTT_ACK_TOPIC
#define MQTT_ACK_TOPIC "register/ack/{hardware_id}"
#endif

namespace {

constexpr char CANONICAL_PREFIX[] = "echoes-device-registration-v1";
// 2020-01-01: minimum sane clock, the JWT `exp` claim needs SNTP first.
constexpr time_t MIN_VALID_TIME = 1577836800;

// The task wakes on every state transition; no polling is needed.
constexpr uint32_t WAKE_INTERVAL_MS = 30000;

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
  // {mac} is the legacy placeholder, still accepted for old .env files.
  for (const char *placeholder : {"{hardware_id}", "{mac}"}) {
    const std::string token(placeholder);
    const size_t pos = topic.find(token);
    if (pos != std::string::npos) {
      topic.replace(pos, token.size(), mac);
    }
  }
  return topic;
}

} // namespace

DeviceRegister::DeviceRegister(DeviceKeys &keys, MqttClient &mqtt,
                                Authenticator &auth, TimeSync &timeSync,
                                WifiSTA &wifiSTA,
                                ConfigurationMode &configurationMode,
                                std::string mac)
    : keys(keys), mqtt(mqtt), auth(auth), timeSync(timeSync),
      wifiSTA(wifiSTA), configurationMode(configurationMode),
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

void DeviceRegister::onConnect(const WifiScanDto & /*connectDto*/) { wake(); }

void DeviceRegister::onDisconnect(const WifiScanDto & /*connectDto*/) {
  wake();
}

void DeviceRegister::onError(const WifiScanDto & /*connectDto*/) { wake(); }

void DeviceRegister::onConfigurationModeChanged(bool /*active*/) { wake(); }

void DeviceRegister::wake() {
  if (task != nullptr) {
    xTaskNotifyGive(task);
  }
}

void DeviceRegister::taskEntry(void *param) {
  static_cast<DeviceRegister *>(param)->run();
}

void DeviceRegister::run() {
  bool ready = false;

  while (true) {
    // A failed round retries at the configured MQTT interval. After a
    // successful round the task only needs to re-evaluate the gate.
    const uint32_t waitMs =
        ready ? WAKE_INTERVAL_MS : MQTT_RECONNECT_INTERVAL_MS;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));

    // Never use the radio for MQTT while the setup AP is in use, and never
    // register before the STA link is up.
    const bool nowReady = wifiSTA.isConnected() &&
                          !configurationMode.isActive() &&
                          !configurationMode.hasApClients();
    if (!nowReady) {
      ready = false;
      continue;
    }
    if (ready) {
      continue;
    }

    // Radio quiet for the whole round: clock sync, TLS registration and the
    // access token.
    for (auto *o : observers) {
      o->onRegistering();
    }
    timeSync.sync();
    const RegisterResult result = registerDevice();

    if (result == RegisterResult::Registered) {
      ready = true;
      if (keys.isLoaded()) {
        logAccessToken();
      }
    } else if (result == RegisterResult::Rejected) {
      // The platform answered and refused the key. Retrying the same round
      // would only spam it; a Wi-Fi/AP transition allows a new attempt.
      ready = true;
    } else {
      // Transient failure (broker/TLS/timeout): keep the gate open and try
      // again on the next period.
      ready = false;
    }
  }
}

DeviceRegister::RegisterResult DeviceRegister::registerDevice() {
  // Prefer the pre-provisioned/stored key; generate one only when absent.
  // A generated key is kept in RAM across transient retries: if the broker
  // received the registration but the ack was lost, generating a new key
  // would conflict with the public key already stored for this MAC.
  bool generated = keyGenerated;
  if (!keys.isLoaded()) {
    if (keys.load()) {
      keyGenerated = false;
      generated = false;
      for (auto *o : observers) {
        o->onKeyLoaded();
      }
    } else {
      generated = true;
      for (auto *o : observers) {
        o->onGeneratingKey(mac);
      }
      if (!keys.create()) {
        for (auto *o : observers) {
          o->onKeyGenerationFailed();
        }
        return RegisterResult::Retry;
      }
      keyGenerated = true;
    }
  }

  const std::string publicKey = trimmed(keys.publicKeyPem());

  if (!mqtt.connect(20000)) {
    retryLater(generated, "MQTT broker unreachable");
    return RegisterResult::Retry;
  }

  // Canonical message signed by the device (device-api.md section 2.3).
  const std::string message =
      std::string(CANONICAL_PREFIX) + "\n" + mac + "\n" + publicKey;
  std::string der;
  if (!keys.sign(message, der)) {
    retryLater(generated, "failed to sign the registration message");
    return RegisterResult::Retry;
  }

  JsonDocument doc;
  // The platform identifies the device by hardware_id (today, the MAC).
  doc["hardware_id"] = mac;
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
    return RegisterResult::Retry;
  }

  std::string ack;
  if (!waitForAck(ack, 15000)) {
    retryLater(generated, "no registration ack (is the platform online?)");
    return RegisterResult::Retry;
  }

  JsonDocument ackDoc;
  if (deserializeJson(ackDoc, ack) != DeserializationError::Ok) {
    retryLater(generated, "malformed registration ack");
    return RegisterResult::Retry;
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
      keyGenerated = false;
      for (auto *o : observers) {
        o->onKeySaved();
      }
    }
    for (auto *o : observers) {
      o->onRegistered(status, deviceId);
    }
    mqtt.disconnect();
    return RegisterResult::Registered;
  }

  for (auto *o : observers) {
    o->onRejected(ackDoc["message"] | "", !generated);
  }
  // A generated key was never stored; a pre-provisioned one stays on flash
  // (only the in-memory copy is dropped, it is reloaded on the next wake).
  failRegistration();
  return RegisterResult::Rejected;
}

void DeviceRegister::failRegistration() {
  mqtt.disconnect();
  keys.reset();
  keyGenerated = false;
}

void DeviceRegister::retryLater(bool generated, const char *reason) {
  for (auto *o : observers) {
    o->onRetryLater(reason, !generated);
  }
  // Keep a generated key in RAM across transient failures: the broker may
  // have stored it even if the ack was lost. It is persisted only after a
  // confirmed registration. A pre-provisioned key remains on flash.
  mqtt.disconnect();
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
