#pragma once

#include <cmath>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "led_color.h"
#include "led_renderer.h"
#include "led_state_config.h"

namespace cyrus_ble {

class CyrusLedIndicator {
 public:
  void set_light(esphome::light::LightState *led);

  void start_boot();
  void finish_boot();

  bool is_boot_active() const { return boot_active_; }
  bool is_boot_finished() const { return boot_finished_; }

  void on_ha_connected();
  void on_volume_request();
  void on_first_mac();
  void on_second_mac();
  void on_bt_connected();
  void on_finished(bool ok);

  void loop();

 private:
  LedRenderer renderer_;
  bool boot_active_{false};
  bool boot_finished_{false};

  LedState state_{LedState::OFF};
  uint32_t state_start_{0};
  LedColor from_color_{0, 0, 0, 0};

  // Fall-to-black transient state
  LedState fall_next_state_{LedState::OFF};
  LedColor fall_base_color_{0, 0, 0, 0};
  float fall_start_brightness_{0.0f};
  uint32_t fall_start_time_{0};
  uint32_t fall_rise_ms_{0};
  uint32_t fall_total_ms_{0};
  bool pending_bt_connected_{false};

  void enter_state(LedState state);
  void enter_pulsing_state(LedState new_state, uint32_t new_period);
  void enter_fall_to_black(LedState next_state);

  LedColor compute_target(const LedStateConfig &config, uint32_t elapsed) const;
  LedColor compute_fall_target(uint32_t now) const;

  static float compute_pulse_factor(const LedStateConfig &config, uint32_t elapsed);
  static LedColor lerp(const LedColor &a, const LedColor &b, float t);
  static float ease(float t);
};

}  // namespace cyrus_ble
