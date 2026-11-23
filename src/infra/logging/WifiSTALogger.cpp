#include "infra/logging/WifiSTALogger.hpp"
#include <Arduino.h>
#include "application/service/RemoteLog.hpp"

void WifiSTALogger::onConnect(const WifiScanDto &connectDto) {
  Serial.printf("[WifiSTA] Connected at %s\n", connectDto.ssid.c_str());
  REMOTE_LOG(Info, Wifi, "WIFI_CONNECTED",
             "Conectado ao Wi-Fi \"%s\" (sinal %d dBm). Abaixo de -80 dBm a conexão tende a cair.",
             connectDto.ssid.c_str(), static_cast<int>(connectDto.rssi));
}

void WifiSTALogger::onConnecting(const WifiScanDto &connectDto) {
  Serial.printf("[WifiSTA] connecting at %s\n", connectDto.ssid.c_str());
  REMOTE_LOG(Debug, Wifi, "WIFI_CONNECTING",
             "Tentando conectar ao Wi-Fi \"%s\".", connectDto.ssid.c_str());
}

void WifiSTALogger::onError(const WifiScanDto &connectDto) {
  Serial.printf("[WifiSTA] error while connecting at %s\n",
                connectDto.ssid.c_str());
  REMOTE_LOG(Error, Wifi, "WIFI_CONNECT_FAIL",
             "Falha ao conectar em \"%s\". Senha incorreta, roteador fora de alcance ou 5 GHz (o ESP32 só usa 2,4 GHz).",
             connectDto.ssid.c_str());
}

void WifiSTALogger::onDisconnect(const WifiScanDto &connectDto) {
  Serial.printf("[WifiSTA] Disconnected from %s\n", connectDto.ssid.c_str());
  REMOTE_LOG(Warn, Wifi, "WIFI_DISCONNECTED",
             "Desconectado do Wi-Fi \"%s\". Roteador desligado, sinal fraco ou rede trocada; reconexão automática.",
             connectDto.ssid.c_str());
}

void WifiSTALogger::onScan(const std::vector<WifiScanDto> &scanList) {
  Serial.printf("[WifiSTA] scan: %d networks\n", (int)scanList.size());
  if (scanList.empty()) {
    REMOTE_LOG(Warn, Wifi, "WIFI_SCAN_EMPTY",
               "Varredura não achou nenhuma rede. Sem roteador por perto ou antena/rádio com problema.");
  }
}
