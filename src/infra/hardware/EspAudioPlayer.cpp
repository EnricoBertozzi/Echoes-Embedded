#include "infra/hardware/EspAudioPlayer.hpp"
#include <Arduino.h>
#include <cstring>
#include <utility>

bool EspAudioPlayer::audioBusy = false;
AudioOutputI2S *EspAudioPlayer::output = nullptr;
SemaphoreHandle_t EspAudioPlayer::audioMutex = nullptr;
EspAudioPlayer *EspAudioPlayer::activePlayer = nullptr;

EspAudioPlayer::EspAudioPlayer(int i2sBlck, int i2sLrc, int i2sDout, int sdMode)
    : sdMode(sdMode) {

  if (!audioMutex) {
    audioMutex = xSemaphoreCreateMutex();
  }

  pinMode(sdMode, OUTPUT);
  digitalWrite(sdMode, LOW);

  if (!output) {
    output = new AudioOutputI2S();
    output->SetPinout(i2sBlck, i2sLrc, i2sDout);
    // Ganho digital maximo sem amplificacao acima da escala 1.0.
    // O ganho fisico depende do MAX98357A, da alimentacao e do alto-falante.
    output->SetGain(1.0);
  }
}

EspAudioPlayer::EspAudioPlayer(int sdMode) : sdMode(sdMode) {
  pinMode(sdMode, OUTPUT);
  digitalWrite(sdMode, LOW);
}

bool EspAudioPlayer::play(const Scenario &scenario) {
  return play(scenario, nullptr);
}

void EspAudioPlayer::onPlayback(EventHandler handler) {
  xSemaphoreTake(audioMutex, portMAX_DELAY);
  eventHandler = std::move(handler);
  xSemaphoreGive(audioMutex);
}

bool EspAudioPlayer::play(const Scenario &scenario, const char *scenarioId) {
  if (scenarioId && std::strlen(scenarioId) != 36) {
    return false;
  }
  xSemaphoreTake(audioMutex, portMAX_DELAY);

  if (audioBusy) {
    xSemaphoreGive(audioMutex);
    return false;
  }

  file = new AudioFileSourceLittleFS(scenario.audio);

  if (!file->isOpen()) {
    delete file;
    file = nullptr;

    xSemaphoreGive(audioMutex);
    return false;
  }

  digitalWrite(sdMode, HIGH);
  vTaskDelay(pdMS_TO_TICKS(10));

  mp3 = new AudioGeneratorMP3();

  if (!mp3->begin(file, output)) {
    stopInternal();

    xSemaphoreGive(audioMutex);
    return false;
  }

  activePlayer = this;
  audioBusy = true;
  startedAtMs = millis();
  playbackEvent = {};
  if (scenarioId) {
    std::memcpy(playbackEvent.scenarioId, scenarioId, 36);
  }
  playbackEvent.type = PlaybackEvent::PLAY;
  if (eventHandler) {
    eventHandler(playbackEvent);
  }

  Serial.printf("[AUDIO] MP3 iniciado, AMP GPIO=%d, gain=1.0\n", sdMode);
  xSemaphoreGive(audioMutex);
  return true;
}

bool EspAudioPlayer::loop() {
  bool finished = false;
  xSemaphoreTake(audioMutex, portMAX_DELAY);

  if (activePlayer == this && mp3) {
    bool running = mp3->loop();

    if (!running) {
      Serial.printf("[AUDIO] Decoder parou apos %lu ms (fim do arquivo ou erro)\n", static_cast<unsigned long>(millis() - startedAtMs));
      stopInternal();
      finished = true;
    }
  }

  xSemaphoreGive(audioMutex);
  return finished;
}

bool EspAudioPlayer::stop() {
  xSemaphoreTake(audioMutex, portMAX_DELAY);

  const bool stopped = activePlayer == this;
  if (stopped) {
    Serial.println("[AUDIO] Encerrando player ativo");
    stopInternal();
  }

  xSemaphoreGive(audioMutex);
  return stopped;
}

void EspAudioPlayer::stopInternal() {
  if (mp3) {
    if (mp3->isRunning()) {
      mp3->stop();
    }

    delete mp3;
    mp3 = nullptr;
  }

  if (file) {
    file->close();
    delete file;
    file = nullptr;
  }

  digitalWrite(sdMode, LOW);

  if (activePlayer == this) {
    activePlayer = nullptr;
    audioBusy = false;
    playbackEvent.type = PlaybackEvent::STOP;
    if (eventHandler) {
      eventHandler(playbackEvent);
    }
    playbackEvent = {};
  }
}
