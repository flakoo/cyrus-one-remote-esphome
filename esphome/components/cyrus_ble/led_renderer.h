#pragma once

#include "led_color.h"
#include "esphome/components/light/light_state.h"

namespace cyrus_ble {

class LedRenderer {
 public:
  void set_light(esphome::light::LightState *led);
  void render(const LedColor &c);
  const LedColor &get_last_color() const { return last_written_; }

 private:
  esphome::light::LightState *led_{nullptr};
  LedColor last_written_{-1.0f, -1.0f, -1.0f, -1.0f};
};

}  // namespace cyrus_ble
