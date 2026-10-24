#include <stddef.h>
#include <stdint.h>
#include "chos/atomic.h"
#include "chos/uart.h"
#include "chos/platform.h"
#include "chos/spinlock.h"

/* Set once from the active platform profile; MMIO stays direct and polled. */
static uintptr_t uart0_base;
static chos_spinlock_t uart_console_lock = CHOS_SPINLOCK_INITIALIZER;
#define UART_DR    (*(volatile uint32_t *)(uart0_base + 0x00))
#define UART_FR    (*(volatile uint32_t *)(uart0_base + 0x18))
#define UART_IBRD  (*(volatile uint32_t *)(uart0_base + 0x24))
#define UART_FBRD  (*(volatile uint32_t *)(uart0_base + 0x28))
#define UART_LCRH  (*(volatile uint32_t *)(uart0_base + 0x2c))
#define UART_CR    (*(volatile uint32_t *)(uart0_base + 0x30))
#define UART_IMSC  (*(volatile uint32_t *)(uart0_base + 0x38))
#define UART_ICR   (*(volatile uint32_t *)(uart0_base + 0x44))
#define UART_FR_RXFE (1u << 4)
#define UART_FR_TXFF (1u << 5)
#define UART_DR_DATA_MASK 0xffu

static void uart_putc_raw(char c) {
    if (!uart0_base) return;
    for (unsigned budget=0; UART_FR & UART_FR_TXFF; ++budget) if (budget==100000u) return;
    UART_DR = (uint32_t)c;
}

void uart_console_emergency_reset(void) {
    /* CHOS is currently single-core; panic never returns to the faulting context. */
    chos_atomic_store_u32(&uart_console_lock.held, 0u);
}

void uart_attach_pl011(uintptr_t base) { uart0_base=base; }

void uart_init(void) {
    uart0_base = platform_uart_base();
    UART_CR = 0;
    UART_ICR = 0x7ff;
    UART_IBRD = 1;
    UART_FBRD = 40;
    UART_LCRH = (3u << 5) | (1u << 4); /* 8 data bits, FIFO enabled. */
    UART_IMSC = 0; /* Polling for now; interrupt-driven I/O comes later. */
    UART_CR = (1u << 0) | (1u << 8) | (1u << 9); /* UART, TX, RX. */
}

uint64_t uart_write_begin(void) {
    return chos_spin_lock_irqsave(&uart_console_lock);
}

void uart_write_end(uint64_t irq_state) {
    chos_spin_unlock_irqrestore(&uart_console_lock, irq_state);
}

/* The *_locked methods MUST only be called inside uart_write_begin/end. */
void uart_putc_locked(char c) {
    if (c == '\n') uart_putc_raw('\r');
    uart_putc_raw(c);
}

void uart_puts_locked(const char *s) {
    if (s == 0) return;
    while (*s != '\0') uart_putc_locked(*s++);
}

void uart_put_u64_locked(uint64_t value) {
    char digits[20];
    size_t count = 0u;
    if (value == 0u) {
        uart_putc_locked('0');
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u) uart_putc_locked(digits[--count]);
}

void uart_putc(char c) {
    uint64_t flags = uart_write_begin();
    uart_putc_locked(c);
    uart_write_end(flags);
}

void uart_puts(const char *s) {
    uint64_t flags = uart_write_begin();
    uart_puts_locked(s);
    uart_write_end(flags);
}

char uart_getc(void) {
    /* Input remains polled from the shell context; it does not hold the TX lock. */
    while (UART_FR & UART_FR_RXFE) { }
    return (char)(UART_DR & UART_DR_DATA_MASK);
}

int uart_try_getc(char *out) {
    if (out == NULL || (UART_FR & UART_FR_RXFE) != 0u) return 0;
    *out = (char)(UART_DR & UART_DR_DATA_MASK);
    return 1;
}
