#pragma once

// Starts/refreshes the network time synchronization; asynchronous and
// idempotent. The device JWT `exp` claim requires a synced clock.
class TimeSync {
public:
  virtual ~TimeSync() = default;

  virtual void sync() = 0;
};
