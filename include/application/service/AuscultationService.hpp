#pragma once

class AuscultationService {
public:
  virtual ~AuscultationService() = default;

  virtual void setup() = 0;
};
