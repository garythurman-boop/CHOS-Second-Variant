#ifndef CHOS_WINDOW_MANAGER_H
#define CHOS_WINDOW_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#define CHOS_WM_MAX_WINDOWS 12u
#define CHOS_WM_NO_WINDOW (-1)

typedef enum {
    CHOS_WM_CONTROL_NONE = 0,
    CHOS_WM_CONTROL_MINIMIZE = 1,
    CHOS_WM_CONTROL_MAXIMIZE = 2,
    CHOS_WM_CONTROL_CLOSE = 3
} chos_wm_title_control_t;

typedef struct {
    int id;
    const char *title;
    int32_t x, y, width, height;
    uint32_t z_order;
    int open;
    int minimized;
    int maximized;
    int32_t restore_x, restore_y, restore_width, restore_height;
} chos_wm_window_t;

typedef struct {
    chos_wm_window_t windows[CHOS_WM_MAX_WINDOWS];
    uint32_t next_z_order;
    int active_id;
} chos_window_manager_t;

void chos_wm_init(chos_window_manager_t *manager);
int chos_wm_open(chos_window_manager_t *manager, int id, const char *title,
                 int32_t x, int32_t y, int32_t width, int32_t height);
int chos_wm_activate(chos_window_manager_t *manager, int id);
int chos_wm_minimize(chos_window_manager_t *manager, int id);
int chos_wm_close(chos_window_manager_t *manager, int id);
int chos_wm_move(chos_window_manager_t *manager, int id, int32_t x, int32_t y,
                 int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y);
int chos_wm_set_minimized(chos_window_manager_t *manager, int id, int minimized);
/* Toggle maximized geometry; the prior normal geometry is preserved for restore. */
int chos_wm_toggle_maximized(chos_window_manager_t *manager, int id,
                             int32_t max_x, int32_t max_y, int32_t max_width, int32_t max_height);
chos_wm_window_t *chos_wm_get(chos_window_manager_t *manager, int id);
const chos_wm_window_t *chos_wm_get_const(const chos_window_manager_t *manager, int id);
const chos_wm_window_t *chos_wm_get_slot(const chos_window_manager_t *manager, size_t slot);
const chos_wm_window_t *chos_wm_topmost(const chos_window_manager_t *manager);
int chos_wm_hit_test(const chos_window_manager_t *manager, int32_t x, int32_t y);
/* Hit-test titlebar controls in physical framebuffer coordinates. The rectangles
 * are derived from the same physical window bounds used by the renderer. */
chos_wm_title_control_t chos_wm_hit_test_title_control(const chos_wm_window_t *window,
                                                        int32_t x, int32_t y,
                                                        uint32_t scale);
int chos_wm_active_id(const chos_window_manager_t *manager);
size_t chos_wm_open_count(const chos_window_manager_t *manager);
size_t chos_wm_visible_count(const chos_window_manager_t *manager);
const chos_wm_window_t *chos_wm_next_in_z_order(const chos_window_manager_t *manager,
                                                 uint32_t after_z_order);

#endif
