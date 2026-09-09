#include "led_renderer.h"
#include "esphome/core/log.h"

namespace cyrus_ble {

void LedRenderer::set_light(esphome::light::LightState *led) {
  led_ = led;
  ESP_LOGI("cyrus", "LED indicator assigned");
}

void LedRenderer::render(const LedColor &c) {
  if (led_ == nullptr)
    return;
  if (c == last_written_)
    return;

  last_written_ = c;
  const bool on = c.brightness > 0.0f;

  ESP_LOGD("cyrus", "LED update: on=%d r=%.2f g=%.2f b=%.2f br=%.2f",
           on, c.r, c.g, c.b, c.brightness);

  auto call = led_->make_call();
  call.set_save(false);
  call.set_publish(false);
  call.set_state(on);
  call.set_transition_length(0);
  if (on) {
    call.set_color_mode(esphome::light::ColorMode::RGB);
    call.set_brightness(c.brightness);
    call.set_color_brightness(1.0f);
    call.set_rgb(c.r, c.g, c.b);
  }
  call.perform();
}

}  // namespace cyrus_ble
