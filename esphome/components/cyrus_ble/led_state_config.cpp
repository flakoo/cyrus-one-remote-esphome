#include "led_state_config.h"

#include <cstddef>

namespace cyrus_ble {

namespace {

constexpr LedStateConfig make_config(
    float r, float g, float b, float brightness,
    bool pulse, float pulse_min, float pulse_max, uint32_t pulse_period_ms,
    uint32_t transition_ms, uint32_t auto_next_ms, LedState next_state,
    bool transition_fade_in = false) {
  return LedStateConfig{
      {r, g, b, brightness},
      pulse, pulse_min, pulse_max, pulse_period_ms,
      transition_ms, auto_next_ms, next_state, transition_fade_in};
}

constexpr LedStateConfig kConfigs[] = {
    // OFF
    make_config(0.0f, 0.0f, 0.0f, 0.0f, false, 0.0f, 0.0f, 0, 0, 0, LedState::OFF),

    // BOOT_STARTUP
    make_config(0.0f, 0.95f, 1.0f, 1.0f, true, 0.50f, 1.0f, 400, 200, 0,
                LedState::BOOT_CONNECTING, true),

    // BOOT_CONNECTING
    make_config(0.0f, 0.95f, 1.0f, 1.0f, true, 0.50f, 1.0f, 1200, 0, 1200,
                LedState::BOOT_CONNECTED),

    // BOOT_CONNECTED
    make_config(0.0f, 0.95f, 1.0f, 1.0f, false, 0.0f, 0.0f, 0, 300, 3300,
                LedState::BOOT_FINISHING),

    // BOOT_FINISHING
    make_config(0.0f, 0.95f, 1.0f, 0.0f, false, 0.0f, 0.0f, 0, 1000, 1000,
                LedState::OFF),

    // VOL_STARTUP
    make_config(0.0f, 1.0f, 0.0f, 1.0f, true, 0.50f, 1.0f, 400, 200, 0,
                LedState::VOL_WAIT_FIRST_MAC, true),

    // VOL_WAIT_FIRST_MAC
    make_config(0.0f, 1.0f, 0.0f, 1.0f, true, 0.50f, 1.0f, 1200, 0, 0,
                LedState::VOL_WAIT_SECOND_MAC),

    // VOL_WAIT_SECOND_MAC
    make_config(0.0f, 1.0f, 0.0f, 1.0f, true, 0.50f, 1.0f, 1200, 200, 0,
                LedState::VOL_CONNECTED, true),

    // VOL_CONNECTED
    make_config(0.0f, 1.0f, 0.0f, 1.0f, false, 0.0f, 0.0f, 0, 300, 0,
                LedState::OFF),

    // VOL_SUCCESS
    make_config(1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, 0.0f, 0, 600, 2600,
                LedState::VOL_FINISHING, true),

    // VOL_FINISHING
    make_config(1.0f, 1.0f, 1.0f, 0.0f, false, 0.0f, 0.0f, 0, 1000, 1000,
                LedState::OFF),

    // FALL_TO_BLACK
    make_config(0.0f, 0.0f, 0.0f, 0.0f, false, 0.0f, 0.0f, 0, 0, 0,
                LedState::OFF),

    // VOL_ERROR
    make_config(1.0f, 0.0f, 0.0f, 1.0f, true, 0.50f, 1.0f, 1000, 200, 0,
                LedState::OFF, true),
};

static_assert(sizeof(kConfigs) / sizeof(kConfigs[0]) ==
                  static_cast<size_t>(LedState::VOL_ERROR) + 1,
              "LedState config table mismatch");

}  // namespace

const LedStateConfig &get_led_state_config(LedState state) {
  const size_t index = static_cast<size_t>(state);
  if (index >= sizeof(kConfigs) / sizeof(kConfigs[0])) {
    static const LedStateConfig kFallback =
        make_config(0.0f, 0.0f, 0.0f, 0.0f, false, 0.0f, 0.0f, 0, 0, 0,
                    LedState::OFF);
    return kFallback;
  }
  return kConfigs[index];
}

const char *led_state_name(LedState state) {
  switch (state) {
    case LedState::OFF: return "OFF";
    case LedState::BOOT_STARTUP: return "BOOT_STARTUP";
    case LedState::BOOT_CONNECTING: return "BOOT_CONNECTING";
    case LedState::BOOT_CONNECTED: return "BOOT_CONNECTED";
    case LedState::BOOT_FINISHING: return "BOOT_FINISHING";
    case LedState::VOL_STARTUP: return "VOL_STARTUP";
    case LedState::VOL_WAIT_FIRST_MAC: return "VOL_WAIT_FIRST_MAC";
    case LedState::VOL_WAIT_SECOND_MAC: return "VOL_WAIT_SECOND_MAC";
    case LedState::VOL_CONNECTED: return "VOL_CONNECTED";
    case LedState::VOL_SUCCESS: return "VOL_SUCCESS";
    case LedState::VOL_FINISHING: return "VOL_FINISHING";
    case LedState::FALL_TO_BLACK: return "FALL_TO_BLACK";
    case LedState::VOL_ERROR: return "VOL_ERROR";
  }
  return "UNKNOWN";
}

}  // namespace cyrus_ble
