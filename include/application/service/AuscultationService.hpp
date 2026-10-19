#pragma once

class AuscultationService {
public:
  ~AuscultationService() = default;

  virtual void setup() = 0;
};
