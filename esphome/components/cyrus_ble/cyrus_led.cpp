#include "cyrus_led.h"
#include "esphome/core/log.h"

namespace cyrus_ble {

void CyrusLedIndicator::set_light(esphome::light::LightState *led) {
  renderer_.set_light(led);
}

void CyrusLedIndicator::start_boot() {
  if (boot_active_ || boot_finished_)
    return;
  boot_active_ = true;
  enter_state(LedState::BOOT_STARTUP);
}

void CyrusLedIndicator::finish_boot() {
  boot_active_ = false;
  boot_finished_ = true;
}

void CyrusLedIndicator::on_ha_connected() {
  if (state_ == LedState::BOOT_STARTUP) {
    enter_pulsing_state(LedState::BOOT_CONNECTING, 1200);
  }
}

void CyrusLedIndicator::on_volume_request() {
  finish_boot();
  enter_state(LedState::VOL_STARTUP);
}

void CyrusLedIndicator::on_first_mac() {
  finish_boot();
  if (state_ == LedState::VOL_STARTUP) {
    enter_pulsing_state(LedState::VOL_WAIT_FIRST_MAC, 1200);
  }
}

void CyrusLedIndicator::on_second_mac() {
  finish_boot();
  enter_pulsing_state(LedState::VOL_WAIT_SECOND_MAC, 1200);
}

void CyrusLedIndicator::on_bt_connected() {
  finish_boot();
  if (state_ == LedState::VOL_CONNECTED)
    return;
  if (state_ == LedState::VOL_WAIT_SECOND_MAC) {
    enter_state(LedState::VOL_CONNECTED);
    return;
  }
  if (state_ == LedState::FALL_TO_BLACK && fall_next_state_ == LedState::VOL_WAIT_SECOND_MAC) {
    pending_bt_connected_ = true;
    return;
  }
  enter_state(LedState::VOL_CONNECTED);
}

void CyrusLedIndicator::on_finished(bool ok) {
  finish_boot();
  if (ok) {
    enter_fall_to_black(LedState::VOL_SUCCESS);
  } else {
    enter_state(LedState::VOL_ERROR);
  }
}

void CyrusLedIndicator::loop() {
  const uint32_t now = esphome::millis();

  // Handle fall-to-black completion and transition to the next state
  if (state_ == LedState::FALL_TO_BLACK) {
    const uint32_t elapsed_fall = now - fall_start_time_;
    if (elapsed_fall >= fall_total_ms_) {
      if (pending_bt_connected_ && fall_next_state_ == LedState::VOL_WAIT_SECOND_MAC) {
        pending_bt_connected_ = false;
        enter_state(LedState::VOL_CONNECTED);
      } else {
        enter_state(fall_next_state_);
      }
    } else {
      renderer_.render(compute_fall_target(now));
      return;
    }
  }

  // Automatic state progression (steady timers / single white pulse)
  const LedStateConfig &config = get_led_state_config(state_);
  if (config.auto_next_ms > 0 && now - state_start_ >= config.auto_next_ms) {
    enter_state(config.next_state);
  }

  const LedStateConfig &current_config = get_led_state_config(state_);
  const LedColor target = compute_target(current_config, now - state_start_);
  renderer_.render(target);
}

void CyrusLedIndicator::enter_state(LedState state) {
  const LedColor last = renderer_.get_last_color();
  from_color_ = (last.brightness < 0.0f) ? LedColor{0, 0, 0, 0} : last;
  state_ = state;
  state_start_ = esphome::millis();
  ESP_LOGD("cyrus", "LED state: %s", led_state_name(state));

  if (state == LedState::OFF) {
    if (boot_active_) {
      finish_boot();
      ESP_LOGI("cyrus", "Boot sequence finished");
    }
  }
}

void CyrusLedIndicator::enter_pulsing_state(LedState new_state, uint32_t new_period) {
  float phase = 0.0f;
  const LedStateConfig &old_config = get_led_state_config(state_);
  const uint32_t now = esphome::millis();
  if (old_config.pulse) {
    const int32_t pulse_elapsed = static_cast<int32_t>(now - state_start_) -
                                   static_cast<int32_t>(old_config.transition_ms);
    if (pulse_elapsed > 0) {
      phase = fmod(pulse_elapsed / static_cast<float>(old_config.pulse_period_ms), 1.0f);
      if (phase < 0.0f)
        phase += 1.0f;
    }
  }

  enter_state(new_state);
  const LedStateConfig &new_config = get_led_state_config(new_state);
  const uint32_t target_elapsed = static_cast<uint32_t>(phase * new_period) + new_config.transition_ms;
  state_start_ = now - target_elapsed;
}

void CyrusLedIndicator::enter_fall_to_black(LedState next_state) {
  fall_next_state_ = next_state;

  const LedColor current = renderer_.get_last_color();
  fall_start_brightness_ = current.brightness;
  fall_base_color_ = {current.r, current.g, current.b, 1.0f};

  const LedStateConfig &config = get_led_state_config(state_);
  const uint32_t now = esphome::millis();
  fall_start_time_ = now;
  fall_rise_ms_ = 0;
  fall_total_ms_ = 300;  // default fade-out for non-pulsing states

  if (config.pulse) {
    const int32_t pulse_elapsed = static_cast<int32_t>(now - state_start_) -
                                   static_cast<int32_t>(config.transition_ms);
    if (pulse_elapsed > 0) {
      float phase = fmod(pulse_elapsed / static_cast<float>(config.pulse_period_ms), 1.0f);
      if (phase < 0.0f)
        phase += 1.0f;
      if (phase >= 0.5f) {
        fall_total_ms_ = static_cast<uint32_t>((1.0f - phase) * config.pulse_period_ms);
      } else {
        fall_rise_ms_ = static_cast<uint32_t>((0.5f - phase) * config.pulse_period_ms);
        fall_total_ms_ = fall_rise_ms_ + config.pulse_period_ms / 2;
      }
    }
  }

  if (fall_total_ms_ == 0)
    fall_total_ms_ = 50;

  enter_state(LedState::FALL_TO_BLACK);
}

LedColor CyrusLedIndicator::compute_target(const LedStateConfig &config, uint32_t elapsed) const {
  if (elapsed < config.transition_ms) {
    const float t = ease(elapsed / static_cast<float>(config.transition_ms));
    if (config.transition_fade_in) {
      const float target_brightness = config.pulse
                                          ? config.color.brightness * config.pulse_min
                                          : config.color.brightness;
      return lerp(from_color_, {config.color.r, config.color.g, config.color.b, target_brightness}, t);
    }
    return lerp(from_color_, config.color, t);
  }

  LedColor target = config.color;
  if (config.pulse) {
    target.brightness *= compute_pulse_factor(config, elapsed);
  }
  return target;
}

LedColor CyrusLedIndicator::compute_fall_target(uint32_t now) const {
  const uint32_t elapsed = now - fall_start_time_;
  float brightness;
  if (fall_rise_ms_ > 0 && elapsed < fall_rise_ms_) {
    const float t = elapsed / static_cast<float>(fall_rise_ms_);
    brightness = fall_start_brightness_ + (1.0f - fall_start_brightness_) * ease(t);
  } else {
    const uint32_t fall_elapsed = elapsed - fall_rise_ms_;
    const uint32_t fall_duration = fall_total_ms_ - fall_rise_ms_;
    float t = fall_duration > 0 ? static_cast<float>(fall_elapsed) / fall_duration : 1.0f;
    if (t > 1.0f)
      t = 1.0f;
    brightness = 1.0f - ease(t);
  }
  if (brightness < 0.0f)
    brightness = 0.0f;
  LedColor target = fall_base_color_;
  target.brightness = brightness;
  return target;
}

float CyrusLedIndicator::compute_pulse_factor(const LedStateConfig &config, uint32_t elapsed) {
  const uint32_t pulse_elapsed = elapsed - config.transition_ms;
  float phase = fmod(pulse_elapsed / static_cast<float>(config.pulse_period_ms), 1.0f);
  if (phase < 0.0f)
    phase += 1.0f;
  // Triangle wave 0 -> 1 -> 0
  const float triangle = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
  // Square curve for perceptually even pulse
  return config.pulse_min + (config.pulse_max - config.pulse_min) * (triangle * triangle);
}

LedColor CyrusLedIndicator::lerp(const LedColor &a, const LedColor &b, float t) {
  return {
      a.r + (b.r - a.r) * t,
      a.g + (b.g - a.g) * t,
      a.b + (b.b - a.b) * t,
      a.brightness + (b.brightness - a.brightness) * t,
  };
}

float CyrusLedIndicator::ease(float t) {
  // Correct ease-in-out quad, reaches exactly 0 at t=0 and 1 at t=1
  if (t < 0.5f)
    return 2.0f * t * t;
  return 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}

}  // namespace cyrus_ble
