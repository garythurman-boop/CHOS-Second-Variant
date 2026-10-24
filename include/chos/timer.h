#ifndef CHOS_TIMER_H
#define CHOS_TIMER_H

#include <stdint.h>

#define CHOS_TIMER_HZ 100u

int timer_init(void);
void timer_stop(void);
int timer_is_ready(void);
uint64_t timer_ticks(void);
uint64_t timer_frequency_hz(void);
uint64_t timer_counter(void);
void timer_handle_irq(void);

#endif
