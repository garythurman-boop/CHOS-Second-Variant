#include <stdint.h>
#include "chos/exception.h"
#include "chos/uart.h"

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    uart_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> (unsigned)shift) & 0x0fu]);
    }
}

static const char *vector_name(uint64_t kind) {
    switch (kind) {
        case 0: return "Synchronous exception";
        case 1: return "IRQ exception";
        case 2: return "FIQ exception";
        case 3: return "SError exception";
        default: return "Unknown vector";
    }
}

static const char *data_fault_status_name(uint64_t dfsc) {
    switch (dfsc & 0x3fu) {
        case 0x00: return "address-size fault, level 0";
        case 0x01: return "address-size fault, level 1";
        case 0x02: return "address-size fault, level 2";
        case 0x03: return "address-size fault, level 3";
        case 0x04: return "translation fault, level 0";
        case 0x05: return "translation fault, level 1";
        case 0x06: return "translation fault, level 2";
        case 0x07: return "translation fault, level 3";
        case 0x09: return "access-flag fault, level 1";
        case 0x0a: return "access-flag fault, level 2";
        case 0x0b: return "access-flag fault, level 3";
        case 0x0d: return "permission fault, level 1";
        case 0x0e: return "permission fault, level 2";
        case 0x0f: return "permission fault, level 3";
        case 0x10: return "synchronous external abort";
        case 0x11: return "tag check fault";
        case 0x21: return "alignment fault";
        default:   return "unclassified data fault";
    }
}

static const char *instruction_fault_status_name(uint64_t ifsc) {
    switch (ifsc & 0x3fu) {
        case 0x00: return "address-size fault, level 0";
        case 0x01: return "address-size fault, level 1";
        case 0x02: return "address-size fault, level 2";
        case 0x03: return "address-size fault, level 3";
        case 0x04: return "translation fault, level 0";
        case 0x05: return "translation fault, level 1";
        case 0x06: return "translation fault, level 2";
        case 0x07: return "translation fault, level 3";
        case 0x09: return "access-flag fault, level 1";
        case 0x0a: return "access-flag fault, level 2";
        case 0x0b: return "access-flag fault, level 3";
        case 0x0d: return "permission fault, level 1";
        case 0x0e: return "permission fault, level 2";
        case 0x0f: return "permission fault, level 3";
        case 0x10: return "synchronous external abort";
        default: return "unclassified instruction fault";
    }
}

static uint64_t read_tcr_el1(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(value));
    return value;
}

static uint64_t read_ttbr0_el1(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(value));
    return value;
}

static uint64_t read_mair_el1(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, mair_el1" : "=r"(value));
    return value;
}

static uint64_t read_sctlr_el1(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));
    return value;
}

static const char *esr_class_name(uint64_t ec) {
    switch (ec) {
        case 0x00: return "Unknown reason";
        case 0x01: return "Trapped WFI/WFE instruction";
        case 0x07: return "Trapped floating-point/SIMD access";
        case 0x0e: return "Illegal execution state";
        case 0x15: return "SVC instruction from AArch64";
        case 0x20: return "Instruction abort from lower EL";
        case 0x21: return "Instruction abort at current EL";
        case 0x22: return "PC alignment fault";
        case 0x24: return "Data abort from lower EL";
        case 0x25: return "Data abort at current EL";
        case 0x26: return "SP alignment fault";
        case 0x2f: return "SError interrupt";
        case 0x3c: return "BRK instruction from AArch64";
        default:   return "Unclassified or unsupported exception class";
    }
}

CHOS_NORETURN void exception_panic(uint64_t esr, uint64_t elr,
                                   uint64_t far, uint64_t spsr,
                                   uint64_t vector_kind) {
    const uint64_t ec = (esr >> 26) & 0x3fu;

    /* A fault may occur while the console lock is held. Panic is terminal, so
       make polled serial diagnostics available again before printing details. */
    uart_console_emergency_reset();
    uart_puts("\n\n*** CHOS KERNEL PANIC ***\n");
    uart_puts("Unhandled ARM64 exception; system halted.\n");
    uart_puts("Vector: ");
    uart_puts(vector_name(vector_kind));
    uart_puts("\nESR_EL1: ");
    print_hex64(esr);
    uart_puts(" (EC ");
    print_hex64(ec);
    uart_puts(")\nCause:   ");
    uart_puts(esr_class_name(ec));
    if (ec == 0x20u || ec == 0x21u) {
        uart_puts("\nInstruction fault status: ");
        uart_puts(instruction_fault_status_name(esr & 0x3fu));
        uart_puts(" (IFSC ");
        print_hex64(esr & 0x3fu);
        uart_putc(')');
        if ((esr & (1ull << 10)) != 0u) uart_puts("\nFAR_EL1 validity: invalid (FnV=1)");
    } else if (ec == 0x24u || ec == 0x25u) {
        uart_puts("\nData fault status: ");
        uart_puts(data_fault_status_name(esr & 0x3fu));
        uart_puts(" (DFSC ");
        print_hex64(esr & 0x3fu);
        uart_putc(')');
        uart_puts("\nAccess: ");
        uart_puts((esr & (1ull << 6)) != 0u ? "write (WnR=1)" : "read (WnR=0)");
        uart_puts("; stage-1 table walk: ");
        uart_puts((esr & (1ull << 7)) != 0u ? "yes (S1PTW=1)" : "no (S1PTW=0)");
        if ((esr & (1ull << 10)) != 0u) uart_puts("\nFAR_EL1 validity: invalid (FnV=1)");
    }
    uart_puts("\nELR_EL1: ");
    print_hex64(elr);
    uart_puts("\nFAR_EL1: ");
    print_hex64(far);
    uart_puts("\nSPSR_EL1: ");
    print_hex64(spsr);
    uart_puts("\nMMU registers at fault time:");
    uart_puts("\n  SCTLR_EL1: "); print_hex64(read_sctlr_el1());
    uart_puts("\n  TCR_EL1:   "); print_hex64(read_tcr_el1());
    uart_puts("\n  TTBR0_EL1: "); print_hex64(read_ttbr0_el1());
    uart_puts("\n  MAIR_EL1:  "); print_hex64(read_mair_el1());
    uart_puts("\n\nCHOS halted in the exception panic handler. Reset the VM to continue.\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}
