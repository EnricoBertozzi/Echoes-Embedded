#pragma once
#include "freertos/FreeRTOS.h"
#include <deque>
#include <vector>
struct FakeQueue {
    size_t capacity, itemSize;
    std::deque<std::vector<char>> items;
};
using QueueHandle_t = FakeQueue *;
inline QueueHandle_t xQueueCreate(size_t count, size_t size) { return new FakeQueue{count, size, {}}; }
inline void vQueueDelete(QueueHandle_t queue) { delete queue; }
inline size_t uxQueueSpacesAvailable(QueueHandle_t queue) { return queue->capacity - queue->items.size(); }
inline int xQueueSend(QueueHandle_t queue, const void *value, TickType_t) {
    if (!uxQueueSpacesAvailable(queue)) return 0;
    const auto *bytes = static_cast<const char *>(value);
    queue->items.emplace_back(bytes, bytes + queue->itemSize);
    return pdTRUE;
}
inline int xQueuePeek(QueueHandle_t queue, void *out, TickType_t) {
    if (queue->items.empty()) return 0;
    std::memcpy(out, queue->items.front().data(), queue->itemSize);
    return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue, void *out, TickType_t ticks) {
    if (!xQueuePeek(queue, out, ticks)) return 0;
    queue->items.pop_front();
    return pdTRUE;
}
