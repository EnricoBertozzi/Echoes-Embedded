#pragma once

#include <cstdint>

// Phones stay associated with the setup AP long after the user stopped
// configuring (the "no internet" network is kept for a while), so the
// association alone cannot drive configuration mode. What really says a
// client is being configured is portal usage.
class PortalActivity {
public:
  virtual ~PortalActivity() = default;

  // Milliseconds since the last request served to the setup portal.
  virtual uint32_t idleMs() const = 0;
};
