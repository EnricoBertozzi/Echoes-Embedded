#include "infra/logging/DeviceRegisterLogger.hpp"
#include <Arduino.h>
#include "application/service/RemoteLog.hpp"

void DeviceRegisterLogger::onWaitingForConfiguration() {
  REMOTE_LOG(Info, Register, "WAIT_CONFIG",
             "Cadastro aguardando: portal de configuração aberto. Feche-o ou desconecte do AP do ESP32.");
  Serial.println("[Register] setup portal active; close it or disconnect from the ESP32 AP to enroll");
}

void DeviceRegisterLogger::onGeneratingKey(const std::string &mac) {
  Serial.printf("[Register] no private.pem: generating an ES256 key pair "
                "for %s\n",
                mac.c_str());
}

void DeviceRegisterLogger::onKeyGenerationFailed() {
  Serial.println("[Register] key pair generation failed");
  REMOTE_LOG(Error, Register, "KEY_GEN_FAIL",
             "Falha ao gerar o par de chaves ES256. Provável falta de memória ou de entropia; o dispositivo não consegue se cadastrar.");
}

void DeviceRegisterLogger::onKeyLoaded() {
  Serial.println("[Register] private.pem loaded; (re)registering with the "
                 "platform");
}

void DeviceRegisterLogger::onRegistered(const std::string &status,
                                        const std::string &deviceId) {
  REMOTE_LOG(Info, Register, "REGISTERED",
             "Dispositivo cadastrado na plataforma (status=%s, id=%.8s).",
             status.c_str(), deviceId.c_str());
  Serial.printf("[Register] registered (%s) deviceId=%s\n", status.c_str(),
                deviceId.c_str());
}

void DeviceRegisterLogger::onRejected(const std::string &message,
                                      bool preProvisioned) {
  Serial.printf("[Register] registration REJECTED: %s\n", message.c_str());
  REMOTE_LOG(Error, Register, "REGISTER_REJECTED",
             "Cadastro recusado pela plataforma: %s.%s",
             message.c_str(),
             preProvisioned ? " A chave pré-provisionada conflita com o cadastro deste MAC; corrigir no servidor." : "");
  if (preProvisioned) {
    Serial.println("[Register] CRITICAL: the pre-provisioned key conflicts "
                   "with the platform registration for this MAC; fix it on "
                   "the server side (device-api.md section 1)");
  }
}

void DeviceRegisterLogger::onRetryLater(const std::string &reason,
                                        bool keepingKey) {
  Serial.printf("[Register] %s\n", reason.c_str());
  REMOTE_LOG(Warn, Register, "REGISTER_RETRY",
             "Cadastro adiado: %s. Nova tentativa automática (chave %s).",
             reason.c_str(), keepingKey ? "mantida" : "descartada");
  if (keepingKey) {
    Serial.println("[Register] keeping the pre-provisioned key; the "
                   "platform will confirm on a later connection");
  }
}

void DeviceRegisterLogger::onKeySaved() {
  Serial.println("[Register] private.pem saved");
  REMOTE_LOG(Info, Register, "KEY_SAVED", "Chave privada gravada na flash.");
}

void DeviceRegisterLogger::onKeySaveFailed() {
  Serial.println("[Register] CRITICAL: registered but private.pem could "
                 "not be saved");
  REMOTE_LOG(Error, Register, "KEY_SAVE_FAIL",
             "Cadastrado, mas a chave privada não foi gravada na flash. Ao reiniciar o dispositivo perderá a identidade; verifique a LittleFS.");
}

void DeviceRegisterLogger::onTokenIssued(const std::string &token) {
  (void)token;
  Serial.println("[Register] platform access token issued (hidden)");
}

void DeviceRegisterLogger::onClockNotSynced() {
  Serial.println("[Register] clock not synced yet; the token will be "
                 "issued on demand");
  REMOTE_LOG(Warn, Register, "CLOCK_NOT_SYNCED",
             "Relógio (SNTP) ainda não sincronizou; sem hora correta o JWT é rejeitado. Verifique acesso à internet/NTP.");
}
