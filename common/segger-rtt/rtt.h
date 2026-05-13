#ifndef RTT_H
#define RTT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rtt_init(void);
void rtt_write(const char *data, uint32_t len);
uint32_t rtt_read(char *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
