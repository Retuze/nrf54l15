/*
 * Lightweight publish-subscribe event bus.
 *
 * Replaces the weak-function-override pattern previously used to route BLE
 * events from softdevice_port to service modules. Multiple listeners per
 * event are supported; registration is static-array based (no heap).
 *
 * Usage:
 *   static void on_ble_evt(const event_t *e) { ... }
 *   event_bus_subscribe(EVENT_BLE_EVT, on_ble_evt);
 *   ...
 *   event_bus_emit(EVENT_BLE_EVT, &raw_evt, sizeof(raw_evt));
 */
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVENT_BLE_EVT = 0,        /* raw ble_evt_t*, dispatched to GATT services */
    EVENT_BLE_CONNECTED,      /* connection established */
    EVENT_BLE_DISCONNECTED,   /* connection lost / timeout */
    EVENT_BLE_READY,          /* SoftDevice enabled + advertising started */
    EVENT_BLE_ADV_TIMEOUT,    /* advertising duration expired (unused currently) */

    EVENT_BUTTON_PRESS,       /* physical button pressed */
    EVENT_BUTTON_RELEASE,     /* physical button released */
    EVENT_BUTTON_MULTI_CLICK, /* multi-click detected, data = &uint16_t count */

    EVENT_LBS_LED_WRITE,      /* LBS LED characteristic written */
    EVENT_LBS_BUTTON_NOTIFY,  /* LBS button notification sent */

    EVENT_SYSTEM_PM_ENTER,    /* entering low-power */
    EVENT_SYSTEM_PM_EXIT,     /* exiting low-power */

    EVENT__MAX
} event_id_t;

typedef struct {
    event_id_t  id;
    const void *data;
    uint16_t    data_len;
} event_t;

typedef void (*event_listener_t)(const event_t *evt);

void event_bus_init(void);
int  event_bus_subscribe(event_id_t id, event_listener_t listener);
int  event_bus_unsubscribe(event_id_t id, event_listener_t listener);
void event_bus_publish(const event_t *evt);

static inline void event_bus_emit(event_id_t id, const void *data,
                                  uint16_t data_len)
{
    event_t evt = { .id = id, .data = data, .data_len = data_len };
    event_bus_publish(&evt);
}

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */
