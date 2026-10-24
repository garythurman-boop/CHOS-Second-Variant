#include "chos/window_manager.h"

static chos_wm_window_t *find_window(chos_window_manager_t *manager, int id) {
    size_t i;
    if (manager == NULL) return NULL;
    for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
        if (manager->windows[i].open && manager->windows[i].id == id) return &manager->windows[i];
    }
    return NULL;
}

static const chos_wm_window_t *find_window_const(const chos_window_manager_t *manager, int id) {
    size_t i;
    if (manager == NULL) return NULL;
    for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
        if (manager->windows[i].open && manager->windows[i].id == id) return &manager->windows[i];
    }
    return NULL;
}

static void normalize_z_order(chos_window_manager_t *manager) {
    size_t rank, i;
    uint32_t previous = 0u;
    if (manager == NULL) return;
    /* Capacity is bounded; repeatedly choose the smallest unranked z value. */
    for (rank = 1u; rank <= CHOS_WM_MAX_WINDOWS; ++rank) {
        chos_wm_window_t *candidate = NULL;
        for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
            chos_wm_window_t *w = &manager->windows[i];
            if (w->open && w->z_order > previous &&
                (candidate == NULL || w->z_order < candidate->z_order)) candidate = w;
        }
        if (candidate == NULL) break;
        candidate->z_order = (uint32_t)rank;
        previous = candidate->z_order;
    }
    manager->next_z_order = (uint32_t)rank;
}

static void raise_window(chos_window_manager_t *manager, chos_wm_window_t *window) {
    if (manager == NULL || window == NULL) return;
    if (manager->next_z_order >= UINT32_MAX - (uint32_t)CHOS_WM_MAX_WINDOWS) normalize_z_order(manager);
    window->z_order = manager->next_z_order++;
    manager->active_id = window->id;
}

static void choose_active(chos_window_manager_t *manager) {
    const chos_wm_window_t *best = NULL;
    size_t i;
    if (manager == NULL) return;
    for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
        const chos_wm_window_t *w = &manager->windows[i];
        if (w->open && !w->minimized && (best == NULL || w->z_order > best->z_order)) best = w;
    }
    manager->active_id = best != NULL ? best->id : CHOS_WM_NO_WINDOW;
}

void chos_wm_init(chos_window_manager_t *manager) {
    size_t i;
    if (manager == NULL) return;
    for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
        manager->windows[i].id = CHOS_WM_NO_WINDOW;
        manager->windows[i].title = NULL;
        manager->windows[i].x = 0; manager->windows[i].y = 0;
        manager->windows[i].width = 0; manager->windows[i].height = 0;
        manager->windows[i].z_order = 0u;
        manager->windows[i].open = 0; manager->windows[i].minimized = 0;
        manager->windows[i].maximized = 0;
        manager->windows[i].restore_x = manager->windows[i].restore_y = 0;
        manager->windows[i].restore_width = manager->windows[i].restore_height = 0;
    }
    manager->next_z_order = 1u;
    manager->active_id = CHOS_WM_NO_WINDOW;
}

int chos_wm_open(chos_window_manager_t *manager, int id, const char *title,
                 int32_t x, int32_t y, int32_t width, int32_t height) {
    size_t i;
    chos_wm_window_t *window;
    if (manager == NULL || id < 0 || title == NULL || width <= 0 || height <= 0) return 0;
    window = find_window(manager, id);
    if (window == NULL) {
        for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
            if (!manager->windows[i].open) { window = &manager->windows[i]; break; }
        }
        if (window == NULL) return 0;
        window->id = id; window->title = title; window->x = x; window->y = y;
        window->width = width; window->height = height; window->open = 1;
        window->minimized = 0; window->maximized = 0; window->z_order = 0u;
        window->restore_x = window->restore_y = 0;
        window->restore_width = window->restore_height = 0;
    } else {
        window->title = title;
        /* Opening an already-open app preserves the user's geometry. */
        if (window->width <= 0 || window->height <= 0) {
            window->x = x; window->y = y; window->width = width; window->height = height;
        }
        window->minimized = 0;
    }
    raise_window(manager, window);
    return 1;
}

int chos_wm_activate(chos_window_manager_t *manager, int id) {
    chos_wm_window_t *window = find_window(manager, id);
    if (window == NULL) return 0;
    window->minimized = 0;
    raise_window(manager, window);
    return 1;
}

int chos_wm_minimize(chos_window_manager_t *manager, int id) {
    chos_wm_window_t *window = find_window(manager, id);
    if (window == NULL) return 0;
    window->minimized = 1;
    if (manager->active_id == id) choose_active(manager);
    return 1;
}

int chos_wm_close(chos_window_manager_t *manager, int id) {
    chos_wm_window_t *window = find_window(manager, id);
    if (window == NULL) return 0;
    window->open = 0;
    window->minimized = 0;
    window->id = CHOS_WM_NO_WINDOW;
    window->title = NULL;
    window->z_order = 0u;
    window->maximized = 0;
    window->restore_x = window->restore_y = 0;
    window->restore_width = window->restore_height = 0;
    if (manager->active_id == id) choose_active(manager);
    return 1;
}

int chos_wm_move(chos_window_manager_t *manager, int id, int32_t x, int32_t y,
                 int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y) {
    chos_wm_window_t *window = find_window(manager, id);
    if (window == NULL || min_x > max_x || min_y > max_y) return 0;
    if (x < min_x) x = min_x;
    if (x > max_x) x = max_x;
    if (y < min_y) y = min_y;
    if (y > max_y) y = max_y;
    window->x = x; window->y = y;
    return 1;
}

int chos_wm_set_minimized(chos_window_manager_t *manager, int id, int minimized) {
    if (minimized) return chos_wm_minimize(manager, id);
    return chos_wm_activate(manager, id);
}

int chos_wm_toggle_maximized(chos_window_manager_t *manager, int id,
                             int32_t max_x, int32_t max_y, int32_t max_width, int32_t max_height) {
    chos_wm_window_t *window = find_window(manager, id);
    if (window == NULL || max_width <= 0 || max_height <= 0) return 0;
    if (window->maximized) {
        window->x = window->restore_x; window->y = window->restore_y;
        window->width = window->restore_width; window->height = window->restore_height;
        window->maximized = 0;
    } else {
        window->restore_x = window->x; window->restore_y = window->y;
        window->restore_width = window->width; window->restore_height = window->height;
        window->x = max_x; window->y = max_y;
        window->width = max_width; window->height = max_height;
        window->maximized = 1;
    }
    raise_window(manager, window);
    return 1;
}

chos_wm_window_t *chos_wm_get(chos_window_manager_t *manager, int id) { return find_window(manager, id); }
const chos_wm_window_t *chos_wm_get_const(const chos_window_manager_t *manager, int id) { return find_window_const(manager, id); }
const chos_wm_window_t *chos_wm_get_slot(const chos_window_manager_t *manager, size_t slot) {
    if (manager == NULL || slot >= CHOS_WM_MAX_WINDOWS || !manager->windows[slot].open) return NULL;
    return &manager->windows[slot];
}

const chos_wm_window_t *chos_wm_topmost(const chos_window_manager_t *manager) {
    const chos_wm_window_t *best = NULL;
    size_t i;
    if (manager == NULL) return NULL;
    for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
        const chos_wm_window_t *w = &manager->windows[i];
        if (w->open && !w->minimized && (best == NULL || w->z_order > best->z_order)) best = w;
    }
    return best;
}

int chos_wm_hit_test(const chos_window_manager_t *manager, int32_t x, int32_t y) {
    uint32_t ceiling = UINT32_MAX;
    size_t i;
    if (manager == NULL) return CHOS_WM_NO_WINDOW;
    for (;;) {
        const chos_wm_window_t *best = NULL;
        for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
            const chos_wm_window_t *w = &manager->windows[i];
            if (w->open && !w->minimized && w->z_order < ceiling &&
                (best == NULL || w->z_order > best->z_order)) best = w;
        }
        if (best == NULL) return CHOS_WM_NO_WINDOW;
        if (x >= best->x && y >= best->y &&
            (int64_t)x < (int64_t)best->x + best->width &&
            (int64_t)y < (int64_t)best->y + best->height) return best->id;
        ceiling = best->z_order;
    }
}

int chos_wm_active_id(const chos_window_manager_t *manager) { return manager == NULL ? CHOS_WM_NO_WINDOW : manager->active_id; }
size_t chos_wm_open_count(const chos_window_manager_t *manager) {
    size_t i, count = 0u;
    if (manager == NULL) return 0u;
    for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) if (manager->windows[i].open) ++count;
    return count;
}
size_t chos_wm_visible_count(const chos_window_manager_t *manager) {
    size_t i, count = 0u;
    if (manager == NULL) return 0u;
    for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) if (manager->windows[i].open && !manager->windows[i].minimized) ++count;
    return count;
}
const chos_wm_window_t *chos_wm_next_in_z_order(const chos_window_manager_t *manager, uint32_t after_z_order) {
    const chos_wm_window_t *best = NULL;
    size_t i;
    if (manager == NULL) return NULL;
    for (i = 0u; i < CHOS_WM_MAX_WINDOWS; ++i) {
        const chos_wm_window_t *w = &manager->windows[i];
        if (w->open && !w->minimized && w->z_order > after_z_order &&
            (best == NULL || w->z_order < best->z_order)) best = w;
    }
    return best;
}


chos_wm_title_control_t chos_wm_hit_test_title_control(const chos_wm_window_t *window,
                                                        int32_t x, int32_t y,
                                                        uint32_t scale) {
    int64_t left, top, width, height;
    int64_t right;
    if (window == NULL || !window->open || window->minimized || scale == 0u ||
        window->width <= 0 || window->height <= 0) return CHOS_WM_CONTROL_NONE;
    /* Keep the hit geometry derived from the physical window bounds. The renderer
       places title controls at offsets 68, 47 and 26 logical pixels from its right
       edge and 4 pixels below the top; using local logical coordinates elsewhere
       introduced rounding/offset drift at scaled and maximized sizes. */
    left = window->x;
    top = (int64_t)window->y + 4 * (int64_t)scale;
    width = window->width;
    height = 18 * (int64_t)scale;
    right = left + width;
    if ((int64_t)y < top || (int64_t)y >= top + height) return CHOS_WM_CONTROL_NONE;
    if ((int64_t)x >= right - 68 * (int64_t)scale &&
        (int64_t)x < right - 50 * (int64_t)scale) return CHOS_WM_CONTROL_MINIMIZE;
    if ((int64_t)x >= right - 47 * (int64_t)scale &&
        (int64_t)x < right - 29 * (int64_t)scale) return CHOS_WM_CONTROL_MAXIMIZE;
    if ((int64_t)x >= right - 26 * (int64_t)scale &&
        (int64_t)x < right - 8 * (int64_t)scale) return CHOS_WM_CONTROL_CLOSE;
    return CHOS_WM_CONTROL_NONE;
}
