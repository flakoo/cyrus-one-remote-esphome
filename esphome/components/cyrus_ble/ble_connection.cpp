#include "ble_connection.h"
#include "cyrus_protocol.h"
#include "host_task.h"
#include "esphome/core/log.h"

#include <cstring>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
}

namespace cyrus_ble {

namespace {

constexpr int WRITE_SLOT_COUNT = 4;

struct WriteSlot {
  BleConnection *self;
  uint16_t len;
  uint8_t data[protocol::MAX_PACKET];
};

WriteSlot s_slots[WRITE_SLOT_COUNT];
QueueHandle_t s_free_slots = nullptr;

void ensure_slot_pool() {
  if (s_free_slots != nullptr)
    return;
  s_free_slots = xQueueCreate(WRITE_SLOT_COUNT, sizeof(WriteSlot *));
  for (auto &slot : s_slots) {
    WriteSlot *ptr = &slot;
    xQueueSend(s_free_slots, &ptr, 0);
  }
}

void trampoline_write(void *arg) {
  auto *slot = static_cast<WriteSlot *>(arg);
  slot->self->write_on_host(slot->data, slot->len);
  if (xQueueSend(s_free_slots, &slot, 0) != pdTRUE) {
    ESP_LOGE("cyrus", "Write slot pool corrupted");
  }
}

}  // namespace

void BleConnection::connect(const ble_addr_t &addr) {
  // Reset discovery handles in case of reuse.
  attr_handle_ = 0;
  attr_def_handle_ = 0;
  attr_end_handle_ = 0;
  cccd_handle_ = 0;
  svc_start_ = 0;
  svc_end_ = 0;
  subscribed_ = false;
  volume_write_in_flight_ = false;
  connecting_ = true;

  struct ble_gap_conn_params conn_params = {0};
  conn_params.scan_itvl = 0x10;
  conn_params.scan_window = 0x10;
  conn_params.itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
  conn_params.itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
  conn_params.latency = 0;
  conn_params.supervision_timeout = 500;
  conn_params.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
  conn_params.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;

  const int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 10 * 1000,
                                 &conn_params, gap_event_cb, this);
  if (rc != 0) {
    ESP_LOGE("cyrus", "Connect failed to start: %d", rc);
    connecting_ = false;
    if (listener_ != nullptr) {
      listener_->on_connection_failed(rc);
    }
  } else {
    ESP_LOGI("cyrus", "Connecting to target");
  }
}

void BleConnection::disconnect() {
  if (conn_handle_ != BLE_HS_CONN_HANDLE_NONE) {
    ESP_LOGI("cyrus", "Disconnecting");
    ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
  }
}

void BleConnection::write_volume(int volume) {
  uint8_t msg[protocol::MAX_PACKET];
  const size_t len = protocol::build_volume_packet(volume, msg);
  volume_write_in_flight_ = true;
  send_command(msg, len);
}

void BleConnection::send_command(const uint8_t *msg, size_t len) {
  // May be called from the main loop: marshal the actual GATT write onto
  // the NimBLE host task (see host_task.h).
  ensure_slot_pool();
  WriteSlot *slot = nullptr;
  if (xQueueReceive(s_free_slots, &slot, 0) != pdTRUE) {
    ESP_LOGW("cyrus", "Write slot pool exhausted, command dropped");
    if (volume_write_in_flight_) {
      volume_write_in_flight_ = false;
      if (listener_ != nullptr) {
        listener_->on_volume_written(false);
      }
    }
    return;
  }
  slot->self = this;
  slot->len = len;
  std::memcpy(slot->data, msg, len);
  if (!host_task_call(trampoline_write, slot)) {
    xQueueSend(s_free_slots, &slot, 0);
    if (volume_write_in_flight_) {
      volume_write_in_flight_ = false;
      if (listener_ != nullptr) {
        listener_->on_volume_written(false);
      }
    }
  }
}

void BleConnection::write_on_host(const uint8_t *msg, size_t len) {
  if (conn_handle_ == BLE_HS_CONN_HANDLE_NONE || attr_handle_ == 0) {
    ESP_LOGW("cyrus", "send_command without active connection, dropped");
    if (volume_write_in_flight_) {
      volume_write_in_flight_ = false;
      if (listener_ != nullptr) {
        listener_->on_volume_written(false);
      }
    }
    return;
  }
  const int rc = ble_gattc_write_flat(conn_handle_, attr_handle_, msg, len,
                                      write_cb, this);
  if (rc != 0) {
    ESP_LOGE("cyrus", "Write failed to start: %d", rc);
    if (volume_write_in_flight_) {
      volume_write_in_flight_ = false;
      if (listener_ != nullptr) {
        listener_->on_volume_written(false);
      }
    }
  }
}

void BleConnection::on_gap_event(const struct ble_gap_event *event) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      handle_connect(event);
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      handle_disconnect(event);
      break;
    case BLE_GAP_EVENT_NOTIFY_RX:
      handle_notify_rx(event);
      break;
    default:
      break;
  }
}

int BleConnection::gap_event_cb(struct ble_gap_event *event, void *arg) {
  auto *self = static_cast<BleConnection *>(arg);
  self->on_gap_event(event);
  return 0;
}

void BleConnection::handle_connect(const struct ble_gap_event *event) {
  connecting_ = false;
  if (event->connect.status == 0) {
    conn_handle_ = event->connect.conn_handle;
    ESP_LOGI("cyrus", "Connected, conn_handle=%d", conn_handle_);
    if (listener_ != nullptr) {
      listener_->on_connected();
    }
    discover_services();
  } else {
    ESP_LOGE("cyrus", "Connect failed: %d", event->connect.status);
    if (listener_ != nullptr) {
      listener_->on_disconnected(event->connect.status);
    }
  }
}

void BleConnection::handle_disconnect(const struct ble_gap_event *event) {
  ESP_LOGI("cyrus", "Disconnected, reason=%d", event->disconnect.reason);
  conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
  attr_handle_ = 0;
  attr_def_handle_ = 0;
  attr_end_handle_ = 0;
  cccd_handle_ = 0;
  svc_start_ = 0;
  svc_end_ = 0;
  subscribed_ = false;
  if (listener_ != nullptr) {
    listener_->on_disconnected(event->disconnect.reason);
  }
}

void BleConnection::handle_notify_rx(const struct ble_gap_event *event) {
  if (event->notify_rx.attr_handle != attr_handle_ || listener_ == nullptr)
    return;

  uint8_t buf[protocol::MAX_PACKET];
  uint16_t len = 0;
  ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), &len);

  uint8_t cmd;
  uint8_t payload[protocol::MAX_PAYLOAD];
  const size_t payload_len = protocol::parse_message(buf, len, &cmd, payload);
  if (payload_len == 0) {
    ESP_LOGW("cyrus", "Invalid notification frame (%d bytes)", len);
    return;
  }
  listener_->on_notification(cmd, payload, payload_len);
}

void BleConnection::discover_services() {
  const int rc = ble_gattc_disc_all_svcs(conn_handle_, svc_disced_cb, this);
  if (rc != 0) {
    abort_with_error("service discovery start");
  }
}

int BleConnection::svc_disced_cb(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_svc *service,
                                 void *arg) {
  (void)conn_handle;
  auto *self = static_cast<BleConnection *>(arg);

  if (error->status == BLE_HS_EDONE || service == nullptr) {
    if (self->svc_start_ != 0) {
      ESP_LOGI("cyrus", "Discovering characteristics");
      self->discover_characteristics();
    } else {
      self->abort_with_error("Cyrus service not found");
    }
    return 0;
  }

  ble_uuid_any_t target_uuid;
  if (ble_uuid_from_str(&target_uuid, protocol::SERVICE_UUID) != 0)
    return 0;

  if (ble_uuid_cmp(&service->uuid.u, &target_uuid.u) == 0) {
    self->svc_start_ = service->start_handle;
    self->svc_end_ = service->end_handle;
    ESP_LOGI("cyrus", "Cyrus service found: %d-%d", self->svc_start_, self->svc_end_);
  }

  return 0;
}

void BleConnection::discover_characteristics() {
  const int rc = ble_gattc_disc_all_chrs(conn_handle_, svc_start_, svc_end_,
                                         chr_disced_cb, this);
  if (rc != 0) {
    abort_with_error("char discovery start");
  }
}

int BleConnection::chr_disced_cb(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_chr *chr,
                                 void *arg) {
  (void)conn_handle;
  auto *self = static_cast<BleConnection *>(arg);

  if (error->status == BLE_HS_EDONE || chr == nullptr) {
    if (self->attr_handle_ != 0) {
      if (self->attr_end_handle_ == 0) {
        // Last characteristic in the service: descriptors run to svc end.
        self->attr_end_handle_ = self->svc_end_;
      }
      self->discover_descriptors();
    } else {
      self->abort_with_error("Cyrus characteristic not found");
    }
    return 0;
  }

  // Track the end of the target characteristic: the def handle of the next
  // characteristic (minus one) bounds its descriptor range.
  if (self->attr_def_handle_ != 0 && chr->def_handle > self->attr_def_handle_ &&
      (self->attr_end_handle_ == 0 || chr->def_handle - 1 < self->attr_end_handle_)) {
    self->attr_end_handle_ = chr->def_handle - 1;
  }

  ble_uuid_any_t target_uuid;
  if (ble_uuid_from_str(&target_uuid, protocol::CHARACTERISTIC_UUID) != 0)
    return 0;

  if (ble_uuid_cmp(&chr->uuid.u, &target_uuid.u) == 0) {
    self->attr_handle_ = chr->val_handle;
    self->attr_def_handle_ = chr->def_handle;
    ESP_LOGI("cyrus", "Cyrus char found, handle=%d", self->attr_handle_);
  }

  return 0;
}

void BleConnection::discover_descriptors() {
  ESP_LOGI("cyrus", "Discovering descriptors %d-%d", attr_handle_, attr_end_handle_);
  const int rc = ble_gattc_disc_all_dscs(conn_handle_, attr_handle_,
                                         attr_end_handle_, dsc_disced_cb, this);
  if (rc != 0) {
    abort_with_error("descriptor discovery start");
  }
}

int BleConnection::dsc_disced_cb(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 uint16_t chr_val_handle,
                                 const struct ble_gatt_dsc *dsc,
                                 void *arg) {
  (void)conn_handle;
  auto *self = static_cast<BleConnection *>(arg);

  if (error->status == BLE_HS_EDONE || dsc == nullptr) {
    if (self->cccd_handle_ != 0) {
      self->subscribe();
    } else {
      self->abort_with_error("CCCD not found");
    }
    return 0;
  }

  if (chr_val_handle != self->attr_handle_)
    return 0;

  ble_uuid_any_t cccd_uuid;
  if (ble_uuid_from_str(&cccd_uuid, protocol::CCCD_UUID) != 0)
    return 0;

  if (ble_uuid_cmp(&dsc->uuid.u, &cccd_uuid.u) == 0) {
    self->cccd_handle_ = dsc->handle;
    ESP_LOGI("cyrus", "CCCD found, handle=%d", self->cccd_handle_);
  }

  return 0;
}

void BleConnection::subscribe() {
  const uint8_t enable_notify[2] = {0x01, 0x00};
  const int rc = ble_gattc_write_flat(conn_handle_, cccd_handle_, enable_notify,
                                      sizeof(enable_notify), subscribe_cb, this);
  if (rc != 0) {
    abort_with_error("subscribe start");
  }
}

int BleConnection::subscribe_cb(uint16_t conn_handle,
                                const struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg) {
  (void)conn_handle;
  (void)attr;
  auto *self = static_cast<BleConnection *>(arg);

  if (error->status != 0) {
    self->abort_with_error("subscribe write");
    return 0;
  }

  self->subscribed_ = true;
  ESP_LOGI("cyrus", "Subscribed to notifications");
  if (self->listener_ != nullptr) {
    self->listener_->on_subscribed();
  }
  return 0;
}

int BleConnection::write_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg) {
  (void)conn_handle;
  (void)attr;
  auto *self = static_cast<BleConnection *>(arg);

  // Cyrus returns error 0x10E (GATT Unlikely Error / MTU bug), but command works
  const bool ok = (error->status == 0 || error->status == 0x10E);
  if (!ok) {
    ESP_LOGE("cyrus", "Write error: %d", error->status);
  }

  if (self->volume_write_in_flight_) {
    self->volume_write_in_flight_ = false;
    ESP_LOGI("cyrus", "Volume write %s", ok ? "successful" : "failed");
    if (self->listener_ != nullptr) {
      self->listener_->on_volume_written(ok);
    }
  }

  return 0;
}

void BleConnection::abort_with_error(const char *stage) {
  ESP_LOGE("cyrus", "Aborting: %s", stage);
  if (volume_write_in_flight_) {
    volume_write_in_flight_ = false;
    if (listener_ != nullptr) {
      listener_->on_volume_written(false);
    }
  }
  disconnect();
}

}  // namespace cyrus_ble
