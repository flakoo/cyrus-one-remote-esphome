#pragma once

#include <cstdint>
#include "led_color.h"

namespace cyrus_ble {

enum class LedState {
  OFF,
  BOOT_STARTUP,
  BOOT_CONNECTING,
  BOOT_CONNECTED,
  BOOT_FINISHING,
  VOL_STARTUP,
  VOL_WAIT_FIRST_MAC,
  VOL_WAIT_SECOND_MAC,
  VOL_CONNECTED,
  VOL_SUCCESS,
  VOL_FINISHING,
  FALL_TO_BLACK,
  VOL_ERROR
};

struct LedStateConfig {
  LedColor color;            // target RGB + master brightness
  bool pulse{false};         // pulse after transition?
  float pulse_min{0.0f};     // pulse minimum brightness multiplier
  float pulse_max{0.0f};     // pulse maximum brightness multiplier
  uint32_t pulse_period_ms{0};  // pulse period
  uint32_t transition_ms{0};    // transition duration on state entry
  uint32_t auto_next_ms{0};     // automatic next-state after this many ms (0 = never)
  LedState next_state{LedState::OFF};  // automatic next state
  bool transition_fade_in{false};    // fade in on entry (from black/previous color)
};

const LedStateConfig &get_led_state_config(LedState state);
const char *led_state_name(LedState state);

}  // namespace cyrus_ble
