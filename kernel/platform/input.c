#include "chos/input.h"
#include "chos/gui.h"
chos_input_diagnostics_t chos_input_diagnostics;
void chos_input_key(uint16_t code, int down) { ++chos_input_diagnostics.keys; chos_gui_input_key(code, down); }
void chos_input_button(uint16_t code, int down) { ++chos_input_diagnostics.buttons; chos_gui_input_button(code, down); }
void chos_input_relative(int32_t x, int32_t y) { ++chos_input_diagnostics.relative; chos_gui_input_pointer_relative(x, y); }
void chos_input_scroll(int32_t wheel) { ++chos_input_diagnostics.wheel; chos_gui_input_scroll(wheel); }
