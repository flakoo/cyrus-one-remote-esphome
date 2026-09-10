#include "cyrus_ble.h"
#include "cyrus_protocol.h"
#include "host_task.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cyrus_ble {

static constexpr const char *TAG = "cyrus";

CyrusBleComponent *CyrusBleComponent::instance_ = nullptr;

CyrusBleComponent::CyrusBleComponent() {
  scanner_.set_listener(this);
  connection_.set_listener(this);
}

void CyrusBleComponent::set_status_sensor(esphome::binary_sensor::BinarySensor *status_sensor) {
  status_sensor_ = status_sensor;
}

void CyrusBleComponent::set_led(esphome::light::LightState *led) {
  led_indicator_.set_light(led);
}

void CyrusBleComponent::on_ha_connected() {
  led_indicator_.on_ha_connected();
}

void CyrusBleComponent::setup() {
  ESP_LOGI(TAG, "Initializing native NimBLE");

  notification_queue_ = xQueueCreate(8, sizeof(BleNotification));
  if (notification_queue_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create notification queue");
    return;
  }

  const int rc = nimble_port_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
    return;
  }
  host_task_init();

  instance_ = this;
  ble_hs_cfg.sync_cb = sync_cb;
  ble_hs_cfg.reset_cb = reset_cb;

  nimble_port_freertos_init(nimble_host_task);
}

// ------------------------------------------------------------------
// Public control API (called from ESPHome API services, main loop)
// ------------------------------------------------------------------

void CyrusBleComponent::request_volume(int volume) {
  target_volume_ = std::max(0, std::min(90, volume));
  led_indicator_.on_volume_request();

  if (connection_.is_connected()) {
    ESP_LOGI(TAG, "Already connected, writing volume=%d", target_volume_);
    connection_.write_volume(target_volume_);
    return;
  }

  ESP_LOGI(TAG, "Volume request: %d", target_volume_);
  start_scanning("volume request");
}

void CyrusBleComponent::set_volume(int volume) {
  volume = std::max(0, std::min(90, volume));
  uint8_t msg[protocol::MAX_PACKET];
  enqueue_command(msg, protocol::build_volume_packet(volume, msg));
  enqueue_fetch(protocol::Cmd::VOLUME);
}

void CyrusBleComponent::set_mute(bool enabled) {
  uint8_t msg[protocol::MAX_PACKET];
  enqueue_command(msg, protocol::build_bool_packet(protocol::Cmd::MUTE, enabled, msg));
}

void CyrusBleComponent::set_av_direct(bool enabled) {
  uint8_t msg[protocol::MAX_PACKET];
  enqueue_command(msg, protocol::build_bool_packet(protocol::Cmd::AV_DIRECT, enabled, msg));
}

void CyrusBleComponent::set_balance(int balance) {
  uint8_t msg[protocol::MAX_PACKET];
  enqueue_command(msg, protocol::build_balance_packet(balance, msg));
  enqueue_fetch(protocol::Cmd::BALANCE);
}

void CyrusBleComponent::set_source(const std::string &source) {
  size_t count;
  const char *const *list = source_list(&count);
  for (size_t i = 0; i < count; i++) {
    if (source == list[i]) {
      uint8_t msg[protocol::MAX_PACKET];
      enqueue_command(msg, protocol::build_source_packet(i + 1, msg));
      enqueue_fetch(protocol::Cmd::SOURCE);
      return;
    }
  }
  ESP_LOGW(TAG, "Unknown source '%s' for current model", source.c_str());
}

void CyrusBleComponent::brightness_up() {
  uint8_t msg[protocol::MAX_PACKET];
  enqueue_command(msg, protocol::build_brightness_packet(true, msg));
}

void CyrusBleComponent::brightness_down() {
  uint8_t msg[protocol::MAX_PACKET];
  enqueue_command(msg, protocol::build_brightness_packet(false, msg));
}

// ------------------------------------------------------------------
// Main loop
// ------------------------------------------------------------------

void CyrusBleComponent::loop() {
  if (state_ == State::IDLE) {
    led_indicator_.start_boot();
    // Always-on relay: never stay idle.
    start_scanning("idle recovery");
  }
  led_indicator_.loop();

  const uint32_t now = esphome::millis();

  // Legacy status pulse (500 ms) for the HA power-on automation.
  if (state_ == State::PULSE && now - pulse_start_ >= 500) {
    if (status_sensor_ != nullptr) {
      status_sensor_->publish_state(false);
    }
  }

  // Flags set on the NimBLE host task.
  if (subscribed_pending_) {
    subscribed_pending_ = false;
    ESP_LOGI(TAG, "Link ready");
    set_link_state(true, true);
    led_indicator_.on_bt_connected();
    if (connection_.has_pending_volume()) {
      // Direct write (not queued): the write completion drives the
      // legacy status pulse that the HA power-on automation waits for.
      connection_.clear_pending_volume();
      connection_.write_volume(target_volume_);
    }
    enqueue_state_refresh();
  }

  if (volume_write_result_ >= 0) {
    const bool ok = volume_write_result_ == 1;
    volume_write_result_ = -1;
    publish_status(ok);
  }

  if (link_lost_pending_) {
    link_lost_pending_ = false;
    ESP_LOGW(TAG, "Link lost");
    set_link_state(false, false);
    command_queue_.clear();
    if (state_ == State::CONNECTED || state_ == State::READY || state_ == State::PULSE) {
      // The amp may still be powered: rescan (times out on its own if off).
      start_scanning("link lost");
    } else {
      state_ = State::IDLE;
    }
  }

  // Amp state notifications.
  BleNotification n;
  while (notification_queue_ != nullptr &&
         xQueueReceive(notification_queue_, &n, 0) == pdTRUE) {
    process_notification(n);
  }

  // Outgoing command queue with spacing.
  if (!command_queue_.empty()) {
    if (!connection_.is_connected()) {
      ESP_LOGW(TAG, "Dropping %d queued commands (disconnected)",
               (int)command_queue_.size());
      command_queue_.clear();
    } else if (now - last_command_sent_ >= COMMAND_SPACING_MS) {
      const auto msg = command_queue_.front();
      command_queue_.pop_front();
      connection_.send_command(msg.data(), msg.size());
      last_command_sent_ = now;
    }
  }

  if (state_ != State::SCANNING)
    return;

  if (now - scan_start_time_ >= SCAN_TIMEOUT_MS) {
    on_scan_timeout();
  } else if (scanner_.boot_stage() == 2 && scanner_.mac_a_time() != 0 &&
             (now - scanner_.mac_a_time()) >= MAC_A_TO_B_TIMEOUT_MS) {
    on_mac_b_timeout();
  }
}

// ------------------------------------------------------------------
// NimBLE plumbing
// ------------------------------------------------------------------

CyrusBleComponent *CyrusBleComponent::instance() {
  return instance_;
}

void CyrusBleComponent::nimble_host_task(void *param) {
  (void)param;
  ESP_LOGI(TAG, "NimBLE host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void CyrusBleComponent::sync_cb() {
  ESP_LOGI(TAG, "NimBLE host synced");
  if (instance() != nullptr) {
    // The relay is always-on: start scanning right after boot.
    instance()->start_scanning("boot");
  }
}

void CyrusBleComponent::reset_cb(int reason) {
  ESP_LOGE(TAG, "NimBLE host reset: %d", reason);
}

// NimBLE host API calls are only legal on the host task - these trampolines
// route them through the dispatcher (see host_task.h).
static void trampoline_restart_scan(void *arg) {
  static_cast<CyrusBleComponent *>(arg)->restart_scan_on_host();
}

static void trampoline_connect_first(void *arg) {
  static_cast<CyrusBleComponent *>(arg)->connect_first_mac_on_host();
}

void CyrusBleComponent::restart_scan_on_host() {
  if (!ble_hs_synced()) {
    ESP_LOGI(TAG, "NimBLE not synced yet, scan will start after sync");
    return;
  }
  if (scanner_.is_scanning()) {
    scanner_.stop();
  }
  scanner_.start();
}

void CyrusBleComponent::connect_first_mac_on_host() {
  if (scanner_.is_scanning()) {
    scanner_.stop();
  }
  connection_.set_pending_volume(target_volume_);
  connection_.connect(first_addr_);
}

void CyrusBleComponent::start_scanning(const char *reason) {
  ESP_LOGD(TAG, "Scan requested: %s", reason);
  state_ = State::SCANNING;
  scan_start_time_ = esphome::millis();
  host_task_call(trampoline_restart_scan, this);
}

// ------------------------------------------------------------------
// Scanner / connection callbacks (NimBLE host task)
// ------------------------------------------------------------------

void CyrusBleComponent::on_first_mac(const ble_addr_t &addr) {
  first_addr_ = addr;
  led_indicator_.on_first_mac();
}

void CyrusBleComponent::on_second_mac(const ble_addr_t &addr) {
  second_addr_ = addr;
  state_ = State::READY;
  led_indicator_.on_second_mac();
  scanner_.stop();
  connection_.set_pending_volume(target_volume_);
  connection_.connect(addr);
}

void CyrusBleComponent::on_connected() {
  state_ = State::CONNECTED;
  led_indicator_.on_bt_connected();
}

void CyrusBleComponent::on_connection_failed(int reason) {
  ESP_LOGE(TAG, "Connection failed: %d", reason);
  state_ = State::IDLE;
  publish_status(false);
  set_link_state(false, false);
}

void CyrusBleComponent::on_disconnected(int reason) {
  link_lost_pending_ = true;
}

void CyrusBleComponent::on_subscribed() {
  subscribed_pending_ = true;
}

void CyrusBleComponent::on_volume_written(bool ok) {
  volume_write_result_ = ok ? 1 : 0;
}

void CyrusBleComponent::on_notification(uint8_t cmd, const uint8_t *payload, size_t len) {
  if (notification_queue_ == nullptr)
    return;
  BleNotification n{};
  n.cmd = cmd;
  n.len = std::min(len, sizeof(n.payload));
  std::memcpy(n.payload, payload, n.len);
  if (xQueueSend(notification_queue_, &n, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Notification queue full, dropping cmd=0x%02X", cmd);
  }
}

// ------------------------------------------------------------------
// Notification processing (main loop)
// ------------------------------------------------------------------

void CyrusBleComponent::process_notification(const BleNotification &n) {
  switch (n.cmd) {
    case protocol::Cmd::DAC_PRESENCE:
      is_hd_ = (n.len == 1 && n.payload[0] == '1');
      model_known_ = true;
      if (model_sensor_ != nullptr) {
        model_sensor_->publish_state(is_hd_ ? "Cyrus ONE HD" : "Cyrus ONE");
      }
      break;

    case protocol::Cmd::MUTE:
      apply_bool_payload(n.cmd, n.payload, n.len);
      // Mute also toggles headphone routing on the amp.
      enqueue_fetch(protocol::Cmd::HEADPHONE);
      break;

    case protocol::Cmd::AV_DIRECT:
    case protocol::Cmd::HEADPHONE:
      apply_bool_payload(n.cmd, n.payload, n.len);
      break;

    case protocol::Cmd::VOLUME: {
      const int raw = ascii_payload_to_int(n.payload, n.len);
      if (volume_sensor_ != nullptr) {
        volume_sensor_->publish_state(raw);
      }
      if (volume_number_ != nullptr) {
        // Non-linear characteristic (curve factor 15), exposed as 0-100 %.
        const float pct = (powf(15.0f, raw / 90.0f) - 1.0f) / 14.0f * 100.0f;
        volume_number_->publish_state(pct);
      }
      break;
    }

    case protocol::Cmd::BALANCE: {
      const int raw = ascii_payload_to_int(n.payload, n.len);
      if (balance_sensor_ != nullptr) {
        balance_sensor_->publish_state(raw);
      }
      if (balance_number_ != nullptr) {
        balance_number_->publish_state(raw - 10.0f);  // amp 0-20, 10 = center
      }
      break;
    }

    case protocol::Cmd::SW_VERSION: {
      if (sw_version_sensor_ != nullptr) {
        std::string ver;
        for (size_t i = 0; i < n.len; i++) {
          if (i > 0)
            ver += '.';
          ver += static_cast<char>(n.payload[i]);
        }
        sw_version_sensor_->publish_state(ver);
      }
      break;
    }

    case protocol::Cmd::SERIAL_NUMBER:
      if (serial_sensor_ != nullptr) {
        serial_sensor_->publish_state(
            std::string(reinterpret_cast<const char *>(n.payload), n.len));
      }
      break;

    case protocol::Cmd::SOURCE: {
      const int index = ascii_payload_to_int(n.payload, n.len) - 1;
      size_t count;
      const char *const *list = source_list(&count);
      if (index >= 0 && (size_t)index < count) {
        if (source_sensor_ != nullptr) {
          source_sensor_->publish_state(list[index]);
        }
        if (source_select_ != nullptr) {
          source_select_->publish_state(list[index]);
        }
      }
      break;
    }

    default:
      ESP_LOGW(TAG, "Unsupported notification cmd=0x%02X len=%d", n.cmd, n.len);
      break;
  }
}

void CyrusBleComponent::apply_bool_payload(uint8_t cmd, const uint8_t *payload, size_t len) {
  const bool on = (len == 1 && payload[0] == '1');
  switch (cmd) {
    case protocol::Cmd::MUTE:
      if (muted_sensor_ != nullptr) muted_sensor_->publish_state(on);
      if (mute_switch_ != nullptr) mute_switch_->publish_state(on);
      break;
    case protocol::Cmd::AV_DIRECT:
      if (av_direct_sensor_ != nullptr) av_direct_sensor_->publish_state(on);
      if (av_direct_switch_ != nullptr) av_direct_switch_->publish_state(on);
      break;
    case protocol::Cmd::HEADPHONE:
      if (headphones_sensor_ != nullptr) headphones_sensor_->publish_state(on);
      break;
    default:
      break;
  }
}

int CyrusBleComponent::ascii_payload_to_int(const uint8_t *payload, size_t len) {
  int value = 0;
  for (size_t i = 0; i < len; i++) {
    if (payload[i] < '0' || payload[i] > '9')
      break;
    value = value * 10 + (payload[i] - '0');
  }
  return value;
}

const char *const *CyrusBleComponent::source_list(size_t *count) {
  if (is_hd_) {
    *count = sizeof(protocol::SOURCES_ONE_HD) / sizeof(protocol::SOURCES_ONE_HD[0]);
    return protocol::SOURCES_ONE_HD;
  }
  *count = sizeof(protocol::SOURCES_ONE) / sizeof(protocol::SOURCES_ONE[0]);
  return protocol::SOURCES_ONE;
}

// ------------------------------------------------------------------
// Command queue helpers
// ------------------------------------------------------------------

void CyrusBleComponent::enqueue_command(const uint8_t *msg, size_t len) {
  if (command_queue_.size() >= 32) {
    ESP_LOGW(TAG, "Command queue full, dropping oldest");
    command_queue_.pop_front();
  }
  command_queue_.emplace_back(msg, msg + len);
}

void CyrusBleComponent::enqueue_fetch(uint8_t cmd) {
  uint8_t msg[protocol::MAX_PACKET];
  enqueue_command(msg, protocol::build_fetch_packet(cmd, msg));
}

void CyrusBleComponent::enqueue_state_refresh() {
  for (const uint8_t cmd :
       {protocol::Cmd::DAC_PRESENCE, protocol::Cmd::AV_DIRECT, protocol::Cmd::BALANCE,
        protocol::Cmd::HEADPHONE, protocol::Cmd::MUTE, protocol::Cmd::SERIAL_NUMBER,
        protocol::Cmd::SOURCE, protocol::Cmd::SW_VERSION, protocol::Cmd::VOLUME}) {
    enqueue_fetch(cmd);
  }
}

// ------------------------------------------------------------------
// Scan / status helpers
// ------------------------------------------------------------------

void CyrusBleComponent::on_scan_timeout() {
  // Normal state when the amp is off: keep listening indefinitely.
  ESP_LOGD(TAG, "Scan window elapsed, restarting (amp likely off)");
  start_scanning("rescan");
}

void CyrusBleComponent::on_mac_b_timeout() {
  ESP_LOGW(TAG, "MAC B timeout (%lu ms after MAC A), falling back to MAC A",
           MAC_A_TO_B_TIMEOUT_MS);
  scanner_.clear_mac_a_time();
  connect_to_mac_a_fallback();
}

void CyrusBleComponent::connect_to_mac_a_fallback() {
  char addr_str[18];
  snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
           first_addr_.val[5], first_addr_.val[4], first_addr_.val[3],
           first_addr_.val[2], first_addr_.val[1], first_addr_.val[0]);
  ESP_LOGW(TAG, "Fallback: connecting to MAC A %s", addr_str);
  host_task_call(trampoline_connect_first, this);
}

void CyrusBleComponent::publish_status(bool ok) {
  pulse_start_ = esphome::millis();
  state_ = State::PULSE;
  led_indicator_.on_finished(ok);
  if (status_sensor_ != nullptr) {
    status_sensor_->publish_state(ok);
  }
}

void CyrusBleComponent::set_link_state(bool connected, bool ready) {
  if (connected_sensor_ != nullptr) {
    connected_sensor_->publish_state(connected);
  }
  if (ready_sensor_ != nullptr) {
    ready_sensor_->publish_state(ready);
  }
}

}  // namespace cyrus_ble
