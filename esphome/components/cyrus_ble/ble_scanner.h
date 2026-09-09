#pragma once

#include <cstdint>
#include <cstring>

extern "C" {
#include "host/ble_gap.h"
#include "host/ble_hs.h"
}

namespace cyrus_ble {

class BleScannerListener {
 public:
  virtual ~BleScannerListener() = default;
  virtual void on_first_mac(const ble_addr_t &addr) = 0;
  virtual void on_second_mac(const ble_addr_t &addr) = 0;
};

class BleScanner {
 public:
  BleScanner() = default;

  void set_listener(BleScannerListener *listener) { listener_ = listener; }

  void reset();
  void start();
  void stop();

  bool is_scanning() const { return scanning_; }
  int boot_stage() const { return boot_stage_; }
  uint32_t mac_a_time() const { return mac_a_time_; }
  void clear_mac_a_time() { mac_a_time_ = 0; }

  // Dispatches NimBLE GAP events registered during start().
  void on_gap_event(const struct ble_gap_event *event);

 private:
  static int gap_event_cb(struct ble_gap_event *event, void *arg);
  static void addr_to_str(const ble_addr_t *addr, char *buf, size_t len);

  void handle_disc(const struct ble_gap_event *event);

  BleScannerListener *listener_{nullptr};
  int boot_stage_{0};
  ble_addr_t first_addr_{};
  uint32_t mac_a_time_{0};
  bool scanning_{false};
};

}  // namespace cyrus_ble
