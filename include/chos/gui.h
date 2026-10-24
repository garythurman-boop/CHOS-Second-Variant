#ifndef CHOS_GUI_H
#define CHOS_GUI_H

#include <stddef.h>
#include <stdint.h>
#include "chos/gfx.h"
#include "chos/gui_protocol.h"
#include "chos/driver_report.h"

int chos_gui_init(const chos_surface_t *surface);
void chos_gui_show(void);
/* Signal that device/input initialization finished; splash waits for this and its time floor. */
void chos_gui_startup_complete(void);
/* Native hardware reports stay available without input. Recovery is offered
 * after five seconds without the required input functions, never on a keypress. */
void chos_gui_set_driver_report(chos_driver_report_fn report,int (*needs_recovery)(void));
void chos_gui_show_driver_report(void);
/* Report read-only platform PCI inventory to the Welcome screen. Used by the
 * UEFI path so input bring-up can be debugged even without working input. */
void chos_gui_set_pci_input_status(uint32_t pci_devices, uint32_t xhci_count,
                                   uint16_t xhci_vendor, uint16_t xhci_device,
                                   uint32_t xhci_bar0, uint32_t xhci_bar1);
void chos_gui_input_key(uint16_t keycode, int pressed);
void chos_gui_input_key_unicode(uint16_t keycode, uint16_t unicode_char, int pressed);
void chos_gui_input_pointer_relative(int32_t dx, int32_t dy);
void chos_gui_input_pointer_absolute(int32_t x, int32_t y);
void chos_gui_input_scroll(int32_t lines);
void chos_gui_input_button(uint16_t button, int pressed);
void chos_gui_pump(void);
/* Route EL0 write() bytes into the graphical Console whenever it remains open,
 * including when it is minimized or another window has focus. */
int chos_gui_console_output_enabled(void);
void chos_gui_console_write(const char *bytes, size_t length);
/* Read bytes queued for the active user-space Console session. Returns -1 for a non-owner. */
int64_t chos_gui_console_session_read(uint32_t owner_pid, char *destination, size_t capacity);
int chos_gui_ready(void);
const char *chos_gui_view_name(void);
/* EL0 GUI service. Messages have already been copied from the caller's address
 * space and are validated again here; each process may own one GUI window. */
int64_t chos_gui_submit_message(uint32_t owner_pid, const chos_gui_message_t *message, size_t supplied_bytes);
int chos_gui_pop_event(uint32_t owner_pid, chos_gui_message_t *event_out);
void chos_gui_process_exited(uint32_t owner_pid);

#endif
