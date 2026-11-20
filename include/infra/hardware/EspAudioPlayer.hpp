#pragma once

#include "application/service/AudioPlayer.hpp"
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class EspAudioPlayer : public AudioPlayer {
public:
  EspAudioPlayer(int i2sBlck, int i2sLrc, int i2sDout, int sdMode);
  EspAudioPlayer(int sdMode);

  bool play(const Scenario &scenario) override;
  bool stop() override;

  // Retorna true quando o decodificador parou (fim do MP3 ou falha).
  bool loop();

private:
  static bool audioBusy;
  static AudioOutputI2S *output;
  static SemaphoreHandle_t audioMutex;
  static EspAudioPlayer *activePlayer;

  const int sdMode;

  AudioFileSourceLittleFS *file{nullptr};
  AudioGeneratorMP3 *mp3{nullptr};
  uint32_t startedAtMs{0};

  void stopInternal();
};
