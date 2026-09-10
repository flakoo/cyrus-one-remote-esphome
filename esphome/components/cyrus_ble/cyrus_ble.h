#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "ble_scanner.h"
#include "ble_connection.h"
#include "cyrus_led.h"

#include <deque>
#include <string>
#include <vector>

extern "C" {
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
}

namespace cyrus_ble {

class CyrusBleComponent : public esphome::Component,
                          public BleScannerListener,
                          public BleConnectionListener {
 public:
  CyrusBleComponent();

  // Host-task entry points invoked by trampolines (public for linkage).
  void restart_scan_on_host();
  void connect_first_mac_on_host();

  void set_status_sensor(esphome::binary_sensor::BinarySensor *status_sensor);
  void set_led(esphome::light::LightState *led);

  void set_volume_sensor(esphome::sensor::Sensor *s) { volume_sensor_ = s; }
  void set_balance_sensor(esphome::sensor::Sensor *s) { balance_sensor_ = s; }
  void set_source_text_sensor(esphome::text_sensor::TextSensor *s) { source_sensor_ = s; }
  void set_model_text_sensor(esphome::text_sensor::TextSensor *s) { model_sensor_ = s; }
  void set_sw_version_text_sensor(esphome::text_sensor::TextSensor *s) { sw_version_sensor_ = s; }
  void set_serial_text_sensor(esphome::text_sensor::TextSensor *s) { serial_sensor_ = s; }
  void set_connected_binary_sensor(esphome::binary_sensor::BinarySensor *s) { connected_sensor_ = s; }
  void set_ready_binary_sensor(esphome::binary_sensor::BinarySensor *s) { ready_sensor_ = s; }
  void set_muted_binary_sensor(esphome::binary_sensor::BinarySensor *s) { muted_sensor_ = s; }
  void set_headphones_binary_sensor(esphome::binary_sensor::BinarySensor *s) { headphones_sensor_ = s; }
  void set_av_direct_binary_sensor(esphome::binary_sensor::BinarySensor *s) { av_direct_sensor_ = s; }

  void on_ha_connected();

  void setup() override;
  void loop() override;

  // Power-on path used by the HA automation: scan for the amp, connect,
  // write the volume, stay connected.
  void request_volume(int volume);

  // Control commands (require an active connection; otherwise dropped).
  void set_volume(int volume);
  void set_mute(bool enabled);
  void set_av_direct(bool enabled);
  void set_balance(int balance);
  void set_source(const std::string &source);
  void brightness_up();
  void brightness_down();

  // BleScannerListener (NimBLE host task)
  void on_first_mac(const ble_addr_t &addr) override;
  void on_second_mac(const ble_addr_t &addr) override;

  // BleConnectionListener (NimBLE host task - flags/queues only)
  void on_connected() override;
  void on_connection_failed(int reason) override;
  void on_disconnected(int reason) override;
  void on_subscribed() override;
  void on_volume_written(bool ok) override;
  void on_notification(uint8_t cmd, const uint8_t *payload, size_t len) override;

 private:
  enum class State {
    IDLE,
    SCANNING,
    READY,
    CONNECTING,
    CONNECTED,
    PULSE
  };

  static constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
  static constexpr uint32_t MAC_A_TO_B_TIMEOUT_MS = 4500;
  static constexpr uint32_t COMMAND_SPACING_MS = 200;

  struct BleNotification {
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[16];
  };

  void enqueue_command(const uint8_t *msg, size_t len);
  void enqueue_fetch(uint8_t cmd);
  void enqueue_state_refresh();
  void process_notification(const BleNotification &n);
  void apply_bool_payload(uint8_t cmd, const uint8_t *payload, size_t len);
  int ascii_payload_to_int(const uint8_t *payload, size_t len);
  const char *const *source_list(size_t *count);

  void start_scanning(const char *reason);
  void on_scan_timeout();
  void on_mac_b_timeout();
  void connect_to_mac_a_fallback();
  void publish_status(bool ok);
  void set_link_state(bool connected, bool ready);

  // Entity pointers (all optional)
  esphome::binary_sensor::BinarySensor *status_sensor_{nullptr};
  esphome::binary_sensor::BinarySensor *connected_sensor_{nullptr};
  esphome::binary_sensor::BinarySensor *ready_sensor_{nullptr};
  esphome::binary_sensor::BinarySensor *muted_sensor_{nullptr};
  esphome::binary_sensor::BinarySensor *headphones_sensor_{nullptr};
  esphome::binary_sensor::BinarySensor *av_direct_sensor_{nullptr};
  esphome::sensor::Sensor *volume_sensor_{nullptr};
  esphome::sensor::Sensor *balance_sensor_{nullptr};
  esphome::text_sensor::TextSensor *source_sensor_{nullptr};
  esphome::text_sensor::TextSensor *model_sensor_{nullptr};
  esphome::text_sensor::TextSensor *sw_version_sensor_{nullptr};
  esphome::text_sensor::TextSensor *serial_sensor_{nullptr};

  State state_{State::IDLE};
  int target_volume_{36};
  bool is_hd_{false};
  bool model_known_{false};

  BleScanner scanner_;
  BleConnection connection_;
  CyrusLedIndicator led_indicator_;

  ble_addr_t first_addr_{};
  ble_addr_t second_addr_{};

  uint32_t scan_start_time_{0};
  uint32_t pulse_start_{0};
  uint32_t last_command_sent_{0};

  std::deque<std::vector<uint8_t>> command_queue_;
  QueueHandle_t notification_queue_{nullptr};

  // Flags set on the NimBLE host task, consumed in loop().
  volatile bool subscribed_pending_{false};
  volatile bool link_lost_pending_{false};
  volatile int8_t volume_write_result_{-1};  // -1 none, 0 failed, 1 ok

  static CyrusBleComponent *instance_;
  static CyrusBleComponent *instance();

  static void nimble_host_task(void *param);
  static void sync_cb();
  static void reset_cb(int reason);
};

}  // namespace cyrus_ble
