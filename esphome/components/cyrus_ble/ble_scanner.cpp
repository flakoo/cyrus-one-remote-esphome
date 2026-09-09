#include "ble_scanner.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdio>

namespace cyrus_ble {

void BleScanner::reset() {
  boot_stage_ = 0;
  std::memset(&first_addr_, 0, sizeof(first_addr_));
  mac_a_time_ = 0;
  scanning_ = false;
}

void BleScanner::start() {
  reset();
  boot_stage_ = 1;

  struct ble_gap_disc_params disc_params = {0};
  disc_params.passive = 0;
  disc_params.itvl = 0x30;
  disc_params.window = 0x20;
  disc_params.filter_policy = 0;
  disc_params.limited = 0;
  disc_params.filter_duplicates = 0;

  const int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 30 * 1000, &disc_params,
                              gap_event_cb, this);
  if (rc != 0) {
    ESP_LOGE("cyrus", "Scan start failed: %d", rc);
    scanning_ = false;
  } else {
    ESP_LOGI("cyrus", "Scan started");
    scanning_ = true;
  }
}

void BleScanner::stop() {
  if (scanning_) {
    ble_gap_disc_cancel();
    scanning_ = false;
  }
}

void BleScanner::on_gap_event(const struct ble_gap_event *event) {
  switch (event->type) {
    case BLE_GAP_EVENT_DISC:
      handle_disc(event);
      break;
    default:
      break;
  }
}

int BleScanner::gap_event_cb(struct ble_gap_event *event, void *arg) {
  auto *self = static_cast<BleScanner *>(arg);
  self->on_gap_event(event);
  return 0;
}

void BleScanner::addr_to_str(const ble_addr_t *addr, char *buf, size_t len) {
  snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
           addr->val[5], addr->val[4], addr->val[3],
           addr->val[2], addr->val[1], addr->val[0]);
}

void BleScanner::handle_disc(const struct ble_gap_event *event) {
  if (boot_stage_ != 1 && boot_stage_ != 2)
    return;

  struct ble_hs_adv_fields fields;
  const int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
  if (rc != 0)
    return;

  // Cyrus ONE / ONE HD advertise as "ONE-<number>", where the number is
  // unit-specific and changes on every power cycle (both the MAC and the
  // advertised name suffix). The only stable identifier is the "ONE-" prefix,
  // which mirrors the local_name glob "ONE-*" used by the Home Assistant
  // integration: https://github.com/vitkuv/cyrus-one-hass
  if (fields.name_len < 4 || fields.name == nullptr)
    return;
  if (std::memcmp(fields.name, "ONE-", 4) != 0)
    return;

  char addr_str[18];
  addr_to_str(&event->disc.addr, addr_str, sizeof(addr_str));
  ESP_LOGD("cyrus", "Found %.*s at %s", fields.name_len,
           reinterpret_cast<const char *>(fields.name), addr_str);

  if (boot_stage_ == 1) {
    std::memcpy(&first_addr_, &event->disc.addr, sizeof(ble_addr_t));
    boot_stage_ = 2;
    mac_a_time_ = esphome::millis();
    if (listener_ != nullptr) {
      listener_->on_first_mac(event->disc.addr);
    }
    ESP_LOGD("cyrus", "MAC A (boot): %s", addr_str);
  } else if (boot_stage_ == 2) {
    if (std::memcmp(&first_addr_, &event->disc.addr, sizeof(ble_addr_t)) != 0) {
      boot_stage_ = 3;
      stop();
      if (listener_ != nullptr) {
        listener_->on_second_mac(event->disc.addr);
      }
      ESP_LOGI("cyrus", "MAC B (stable): %s", addr_str);
    }
  }
}

}  // namespace cyrus_ble
