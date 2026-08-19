#pragma once

#include <cstdint>
#include <functional>

namespace OperatingMode {

enum class Mode : uint8_t {
  Online,
  Offline,
};

using ChangeHandler = std::function<void(Mode mode)>;

bool begin(ChangeHandler changeHandler = nullptr);
void loop();
Mode current();

}  // namespace OperatingMode
