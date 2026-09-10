#pragma once

namespace cyrus_ble {

// NimBLE host API (ble_gap_*, ble_gattc_*) must only be called from the
// NimBLE host task: the FreeRTOS port does not synchronize its event queue
// across tasks (assert in npl_freertos_eventq_remove). This dispatcher
// marshals a call onto the host task through its default event queue.

typedef void (*HostTaskFn)(void *arg);

// Allocate the event pool. Call once after nimble_port_init().
void host_task_init();

// Schedule fn(arg) on the NimBLE host task. Returns false when the event
// pool is exhausted (fn is NOT executed then). The caller owns arg and must
// keep it alive until fn runs.
bool host_task_call(HostTaskFn fn, void *arg);

}  // namespace cyrus_ble
