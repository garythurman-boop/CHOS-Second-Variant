#ifndef CHOS_UART_H
#define CHOS_UART_H

#include <stdint.h>

void uart_init(void);
void uart_attach_pl011(uintptr_t base);
/* Terminal single-CPU panic path: bypass locks; caller must mask all DAIF. */
void uart_console_emergency_reset(void);

/* Each standalone write is serialized against other tasks on the current CPU. */
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
int uart_try_getc(char *out);

/* Group several output fragments into one atomic console transaction. */
uint64_t uart_write_begin(void);
void uart_write_end(uint64_t irq_state);
void uart_putc_locked(char c);
void uart_puts_locked(const char *s);
void uart_put_u64_locked(uint64_t value);

#endif
