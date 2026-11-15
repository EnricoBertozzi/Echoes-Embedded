#pragma once

#include <functional>

using OnPressFn = std::function<void()>;
using OnReleaseFn = std::function<void()>;

class PressSensor {
public:
  virtual ~PressSensor() = default;

  virtual void onPress(OnPressFn onPressFn);
  virtual void onRelease(OnReleaseFn onReleaseFn);
};
