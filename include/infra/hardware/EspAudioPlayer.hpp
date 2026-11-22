#pragma once

#include "application/service/AudioPlayer.hpp"
#include "application/service/PlaybackEvent.hpp"
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>

class EspAudioPlayer : public AudioPlayer {
public:
  EspAudioPlayer(int i2sBlck, int i2sLrc, int i2sDout, int sdMode);
  EspAudioPlayer(int sdMode);

  bool play(const Scenario &scenario) override;
  bool play(const Scenario &scenario, const char *scenarioId);
  bool stop() override;

  // Called under the shared audio mutex: the handler must not block/do HTTP.
  using EventHandler = std::function<void(const PlaybackEvent &)>;
  void onPlayback(EventHandler handler);

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
  PlaybackEvent playbackEvent{};
  EventHandler eventHandler;

  void stopInternal();
};
