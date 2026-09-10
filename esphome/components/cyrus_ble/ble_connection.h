#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
}

namespace cyrus_ble {

class BleConnectionListener {
 public:
  virtual ~BleConnectionListener() = default;
  virtual void on_connected() = 0;
  virtual void on_connection_failed(int reason) = 0;
  virtual void on_disconnected(int reason) = 0;
  // Called once the data characteristic is subscribed for notifications:
  // the connection is fully usable from this point on.
  virtual void on_subscribed() = 0;
  virtual void on_volume_written(bool ok) = 0;
  // Raw notification from the amp, already frame-validated. Called on the
  // NimBLE host task - the listener must only enqueue, never block.
  virtual void on_notification(uint8_t cmd, const uint8_t *payload, size_t len) = 0;
};

class BleConnection {
 public:
  BleConnection() = default;

  void set_listener(BleConnectionListener *listener) { listener_ = listener; }

  bool is_connected() const { return conn_handle_ != BLE_HS_CONN_HANDLE_NONE; }
  bool is_subscribed() const { return subscribed_; }
  uint16_t conn_handle() const { return conn_handle_; }

  void set_pending_volume(int volume) { pending_volume_ = volume; }
  void clear_pending_volume() { pending_volume_ = -1; }
  bool has_pending_volume() const { return pending_volume_ >= 0; }

  void connect(const ble_addr_t &addr);
  void disconnect();
  void write_volume(int volume);
  void send_command(const uint8_t *msg, size_t len);
  // Runs the GATT write; must only run on the NimBLE host task (used by the
  // host_task dispatcher).
  void write_on_host(const uint8_t *msg, size_t len);

  // Dispatches NimBLE GAP/GATT events registered during connect().
  void on_gap_event(const struct ble_gap_event *event);

 private:
  static int gap_event_cb(struct ble_gap_event *event, void *arg);
  static int svc_disced_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                           const struct ble_gatt_svc *service, void *arg);
  static int chr_disced_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr, void *arg);
  static int dsc_disced_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                           uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg);
  static int write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg);
  static int subscribe_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr, void *arg);

  void handle_connect(const struct ble_gap_event *event);
  void handle_disconnect(const struct ble_gap_event *event);
  void handle_notify_rx(const struct ble_gap_event *event);
  void discover_services();
  void discover_characteristics();
  void discover_descriptors();
  void subscribe();
  void abort_with_error(const char *stage);

  BleConnectionListener *listener_{nullptr};
  uint16_t conn_handle_{BLE_HS_CONN_HANDLE_NONE};
  uint16_t svc_start_{0};
  uint16_t svc_end_{0};
  uint16_t attr_handle_{0};
  uint16_t attr_def_handle_{0};
  uint16_t attr_end_handle_{0};
  uint16_t cccd_handle_{0};
  bool connecting_{false};
  bool subscribed_{false};
  bool volume_write_in_flight_{false};
  int pending_volume_{-1};
};

}  // namespace cyrus_ble
