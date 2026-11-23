#include "infra/logging/WifiAPLogger.hpp"
#include <Arduino.h>
#include "application/service/RemoteLog.hpp"

void WifiAPLogger::onConnect() {
  clients++;
  Serial.printf("[AP] client joined (%u using the portal)\n", clients);
  REMOTE_LOG(Info, Wifi, "AP_CLIENT_JOINED",
             "Alguém conectou ao portal de configuração (%u no portal). O áudio fica pausado enquanto o portal estiver em uso.",
             static_cast<unsigned>(clients));
}

void WifiAPLogger::onDisconnect() {
  if (clients > 0) {
    clients--;
  }
  Serial.printf("[AP] client left (%u using the portal)\n", clients);
  REMOTE_LOG(Info, Wifi, "AP_CLIENT_LEFT",
             "Cliente saiu do portal de configuração (%u restantes). Com 0, a reprodução volta ao normal.",
             static_cast<unsigned>(clients));
}
