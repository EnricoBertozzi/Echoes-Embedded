#pragma once

#include "application/service/TimeSync.hpp"

class SntpClock : public TimeSync {
public:
  void sync() override;
};
