#pragma once

namespace cyrus_ble {

struct LedColor {
  float r{0.0f};
  float g{0.0f};
  float b{0.0f};
  float brightness{0.0f};

  bool operator==(const LedColor &other) const {
    return r == other.r && g == other.g && b == other.b && brightness == other.brightness;
  }
  bool operator!=(const LedColor &other) const { return !(*this == other); }
};

}  // namespace cyrus_ble
