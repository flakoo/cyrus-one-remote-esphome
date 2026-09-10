#include "host_task.h"
#include "esphome/core/log.h"

extern "C" {
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
}

namespace cyrus_ble {

namespace {

constexpr int POOL_SIZE = 8;

struct HostEvent {
  ble_npl_event ev;
  HostTaskFn fn;
  void *arg;
};

HostEvent pool[POOL_SIZE];
QueueHandle_t free_events = nullptr;

void event_handler(ble_npl_event *ev) {
  // The arg was set to the HostEvent itself in host_task_call().
  auto *he = static_cast<HostEvent *>(ble_npl_event_get_arg(ev));
  he->fn(he->arg);
  if (xQueueSend(free_events, &he, 0) != pdTRUE) {
    ESP_LOGE("cyrus", "Host event pool corrupted");
  }
}

}  // namespace

void host_task_init() {
  if (free_events != nullptr)
    return;
  free_events = xQueueCreate(POOL_SIZE, sizeof(HostEvent *));
  for (auto &e : pool) {
    HostEvent *ptr = &e;
    xQueueSend(free_events, &ptr, 0);
  }
}

bool host_task_call(HostTaskFn fn, void *arg) {
  HostEvent *he = nullptr;
  if (free_events == nullptr ||
      xQueueReceive(free_events, &he, 0) != pdTRUE) {
    ESP_LOGW("cyrus", "Host event pool exhausted, call dropped");
    return false;
  }
  he->fn = fn;
  he->arg = arg;
  ble_npl_event_init(&he->ev, event_handler, he);
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &he->ev);
  return true;
}

}  // namespace cyrus_ble
