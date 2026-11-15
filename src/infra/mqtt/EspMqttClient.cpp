#include "infra/mqtt/EspMqttClient.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <esp_crt_bundle.h>

namespace {

// esp-mqtt (IDF 4.4) registers the TLS transport only as "mqtts"; a
// "ssl://" URI would resolve to no transport at all, so normalize it.
std::string normalizedUri(std::string uri) {
  if (uri.rfind("ssl://", 0) == 0) {
    uri.replace(0, 6, "mqtts://");
  }
  return uri;
}

// AA:BB:CC:DD:EE:FF -> aa-bb-cc-dd-ee-ff (safe for MQTT client ids)
std::string sanitizedMac(const std::string &mac) {
  std::string out;
  out.reserve(mac.size());
  for (const char c : mac) {
    if (c == ':') {
      out += '-';
    } else {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return out;
}

} // namespace

EspMqttClient::EspMqttClient(const std::string &uri, const std::string &mac)
    : uri(normalizedUri(uri)),
      clientId(std::string(MQTT_CLIENT_ID) + "-" + sanitizedMac(mac)) {
  connectedSem = xSemaphoreCreateBinary();

  esp_mqtt_client_config_t config = {};
  config.uri = this->uri.c_str();
  config.client_id = this->clientId.c_str();
  config.keepalive = 60;
  config.disable_auto_reconnect = true; // connect()/disconnect() drive it
  // Verify the broker certificate against the bundled Mozilla roots
  // (esp-tls refuses TLS with no verification option at all).
  config.crt_bundle_attach = esp_crt_bundle_attach;
  if (strlen(MQTT_USERNAME) > 0) {
    config.username = MQTT_USERNAME;
  }
  if (strlen(MQTT_PASSWORD) > 0) {
    config.password = MQTT_PASSWORD;
  }
  client = esp_mqtt_client_init(&config);
  if (client == nullptr) {
    return;
  }
  esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                 &EspMqttClient::onEvent, this);
}

EspMqttClient::~EspMqttClient() {
  if (client != nullptr) {
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    client = nullptr;
  }
  if (connectedSem != nullptr) {
    vSemaphoreDelete(connectedSem);
    connectedSem = nullptr;
  }
}

void EspMqttClient::onMessage(MessageHandler handler) {
  this->handler = std::move(handler);
}

void EspMqttClient::registerObserver(MqttObserver *observer) {
  observers.push_back(observer);
}

void EspMqttClient::unregisterObserver(MqttObserver *observer) {
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

bool EspMqttClient::connect(uint32_t timeoutMs) {
  if (client == nullptr) {
    return false;
  }
  if (started) {
    esp_mqtt_client_stop(client);
    started = false;
  }
  xSemaphoreTake(connectedSem, 0); // drain a stale connection signal

  if (esp_mqtt_client_start(client) != ESP_OK) {
    return false;
  }
  started = true;
  return xSemaphoreTake(connectedSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void EspMqttClient::disconnect() {
  if (client != nullptr && started) {
    esp_mqtt_client_stop(client);
    started = false;
  }
}

bool EspMqttClient::subscribe(const std::string &topic) {
  if (client == nullptr || !started) {
    return false;
  }
  return esp_mqtt_client_subscribe(client, topic.c_str(), MQTT_QOS) != -1;
}

bool EspMqttClient::publish(const std::string &topic,
                            const std::string &payload) {
  if (client == nullptr || !started) {
    return false;
  }
  const int msgId = esp_mqtt_client_publish(client, topic.c_str(),
                                            payload.c_str(), payload.size(),
                                            MQTT_QOS, 0);
  return msgId >= 0;
}

void EspMqttClient::onEvent(void *arg, esp_event_base_t /*base*/, int32_t id,
                            void *data) {
  static_cast<EspMqttClient *>(arg)->handleEvent(
      static_cast<esp_mqtt_event_id_t>(id),
      static_cast<esp_mqtt_event_handle_t>(data));
}

void EspMqttClient::handleEvent(esp_mqtt_event_id_t id,
                                esp_mqtt_event_handle_t event) {
  switch (id) {
  case MQTT_EVENT_CONNECTED:
    xSemaphoreGive(connectedSem);
    for (auto *o : observers) {
      o->onConnected();
    }
    break;
  case MQTT_EVENT_DATA: {
    if (!handler) {
      break;
    }
    // event->data is not null-terminated; event->topic is, but use the
    // length to be safe.
    const std::string topic(event->topic, event->topic_len);
    const std::string payload(event->data, event->data_len);
    handler(topic, payload);
    break;
  }
  case MQTT_EVENT_ERROR: {
    char reason[80] = "connection error";
    if (event->error_handle != nullptr) {
      snprintf(reason, sizeof reason, "type=%d connect_return=%d tls=0x%04x",
               event->error_handle->error_type,
               event->error_handle->connect_return_code,
               event->error_handle->esp_tls_last_esp_err);
    }
    for (auto *o : observers) {
      o->onError(reason);
    }
    break;
  }
  default:
    break;
  }
}
