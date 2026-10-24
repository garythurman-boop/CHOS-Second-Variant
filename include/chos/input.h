#ifndef CHOS_INPUT_H
#define CHOS_INPUT_H
#include <stdint.h>
typedef struct { uint64_t keys, buttons, relative, wheel; } chos_input_diagnostics_t;
/* Monotonic counters for the single GUI/input producer; no report contents or
 * per-event logging. Debugger/diagnostics consumers may snapshot these. */
extern chos_input_diagnostics_t chos_input_diagnostics;
/* Shared normalized Linux/evdev-code input contract, used by USB and VirtIO. */
void chos_input_key(uint16_t code, int down);
void chos_input_button(uint16_t code, int down);
void chos_input_relative(int32_t x, int32_t y);
void chos_input_scroll(int32_t wheel);
#endif
