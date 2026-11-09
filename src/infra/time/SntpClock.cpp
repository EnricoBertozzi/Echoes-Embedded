#include "infra/time/SntpClock.hpp"
#include <Arduino.h>

void SntpClock::sync() {
  // Idempotent: restarts the SNTP request with the current servers.
  configTzTime("UTC", "pool.ntp.org", "time.google.com", "time.nist.gov");
}
