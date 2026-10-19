#pragma once

#include "domain/entity/AuscultationPoint.hpp"
class AuscultationObserver {
public:
  ~AuscultationObserver() = default;

  virtual void onIdentify(const AuscultationPoint &point) {}
};
