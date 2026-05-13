#include "event_bus.h"
#include <string.h>

#define MAX_LISTENERS_PER_EVENT 4

static struct {
    event_listener_t listeners[MAX_LISTENERS_PER_EVENT];
    uint8_t          count;
} s_subs[EVENT__MAX];

void event_bus_init(void)
{
    memset(s_subs, 0, sizeof(s_subs));
}

int event_bus_subscribe(event_id_t id, event_listener_t listener)
{
    if (id >= EVENT__MAX || listener == NULL) {
        return -1;
    }
    if (s_subs[id].count >= MAX_LISTENERS_PER_EVENT) {
        return -2;
    }
    s_subs[id].listeners[s_subs[id].count++] = listener;
    return 0;
}

int event_bus_unsubscribe(event_id_t id, event_listener_t listener)
{
    if (id >= EVENT__MAX) {
        return -1;
    }
    for (uint8_t i = 0; i < s_subs[id].count; ++i) {
        if (s_subs[id].listeners[i] == listener) {
            s_subs[id].listeners[i] =
                s_subs[id].listeners[--s_subs[id].count];
            return 0;
        }
    }
    return -2;
}

void event_bus_publish(const event_t *evt)
{
    if (evt == NULL || evt->id >= EVENT__MAX) {
        return;
    }
    for (uint8_t i = 0; i < s_subs[evt->id].count; ++i) {
        s_subs[evt->id].listeners[i](evt);
    }
}
