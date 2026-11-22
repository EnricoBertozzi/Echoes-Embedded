#pragma once

#include "application/service/PlaybackEvent.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class PlaybackEventQueue {
public:
    PlaybackEventQueue() : queue(xQueueCreate(16, sizeof(PlaybackEvent))) {}
    ~PlaybackEventQueue() {
        if (queue) {
            vQueueDelete(queue);
        }
    }
    PlaybackEventQueue(const PlaybackEventQueue &) = delete;
    PlaybackEventQueue &operator=(const PlaybackEventQueue &) = delete;

    bool ready() const { return queue != nullptr; }

    // Producers hold the audio mutex; reserve a STOP slot and never wait in the decoder callback.
    bool enqueue(const PlaybackEvent &event) {
        if (event.type == PlaybackEvent::PLAY) {
            tracking = queue && uxQueueSpacesAvailable(queue) >= 2 &&
                       xQueueSend(queue, &event, 0) == pdTRUE;
            return tracking;
        }
        if (!tracking) {
            return true; // PLAY was dropped; do not send an orphan STOP.
        }
        tracking = false;
        return xQueueSend(queue, &event, 0) == pdTRUE;
    }

    // Only the network task consumes; keep the head until HTTP 202 so retries stay ordered.
    bool peek(PlaybackEvent &event) {
        return queue && xQueuePeek(queue, &event, 0) == pdTRUE;
    }
    void accept() {
        PlaybackEvent event;
        if (queue) {
            xQueueReceive(queue, &event, 0);
        }
    }

private:
    QueueHandle_t queue;
    bool tracking = false;
};
