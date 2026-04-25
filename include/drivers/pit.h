#ifndef __PIT_H_
#define __PIT_H_

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void pit_init(uint32_t hz);
void pit_on_irq_tick(void);
uint32_t pit_get_ticks(void);
uint32_t pit_get_frequency_hz(void);
uint32_t pit_get_millis_in_second(void);

#ifdef __cplusplus
}
#endif

#endif
