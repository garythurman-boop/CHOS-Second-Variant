#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "chos/window_manager.h"


static void test_title_control_hitboxes(void) {
    chos_wm_window_t window;
    uint32_t scale;
    const int32_t y = 80;
    const int32_t x = 100;
    memset(&window, 0, sizeof(window));
    window.open = 1;
    window.x = x;
    window.y = y;
    for (scale = 1u; scale <= 3u; ++scale) {
        int32_t width = (int32_t)(595u * scale);
        int32_t button_y = y + (int32_t)(4u * scale);
        int32_t button_h = (int32_t)(18u * scale);
        int32_t right = x + width;
        window.width = width;
        window.height = (int32_t)(465u * scale);
        assert(chos_wm_hit_test_title_control(&window,
            right - (int32_t)(68u * scale) + (int32_t)(9u * scale),
            button_y + button_h / 2, scale) == CHOS_WM_CONTROL_MINIMIZE);
        assert(chos_wm_hit_test_title_control(&window,
            right - (int32_t)(47u * scale) + (int32_t)(9u * scale),
            button_y + button_h / 2, scale) == CHOS_WM_CONTROL_MAXIMIZE);
        assert(chos_wm_hit_test_title_control(&window,
            right - (int32_t)(26u * scale) + (int32_t)(9u * scale),
            button_y + button_h / 2, scale) == CHOS_WM_CONTROL_CLOSE);
        assert(chos_wm_hit_test_title_control(&window,
            right - (int32_t)(8u * scale), button_y + button_h / 2, scale) == CHOS_WM_CONTROL_NONE);
        assert(chos_wm_hit_test_title_control(&window, right - 1,
            button_y + button_h / 2, scale) == CHOS_WM_CONTROL_NONE);
    }
    window.minimized = 1;
    assert(chos_wm_hit_test_title_control(&window, x + window.width - 10, y + 10, 1u) == CHOS_WM_CONTROL_NONE);
}

int main(void) {
    test_title_control_hitboxes();
    chos_window_manager_t manager;
    const chos_wm_window_t *window;
    chos_wm_init(&manager);
    assert(chos_wm_open(&manager, 1, "Console", 20, 30, 200, 150));
    assert(chos_wm_active_id(&manager) == 1);
    assert(chos_wm_open(&manager, 2, "Files", 40, 50, 220, 160));
    assert(chos_wm_active_id(&manager) == 2);
    assert(chos_wm_open_count(&manager) == 2u);
    assert(chos_wm_visible_count(&manager) == 2u);
    assert(chos_wm_hit_test(&manager, 45, 55) == 2); /* topmost overlap wins */
    assert(chos_wm_hit_test(&manager, 25, 35) == 1); /* exposed edge */

    assert(chos_wm_minimize(&manager, 2));
    assert(chos_wm_active_id(&manager) == 1);
    assert(chos_wm_hit_test(&manager, 45, 55) == 1);
    assert(chos_wm_activate(&manager, 2));
    assert(chos_wm_active_id(&manager) == 2);
    window = chos_wm_get_const(&manager, 2);
    assert(window != NULL && !window->minimized && window->x == 40 && window->y == 50);

    assert(chos_wm_move(&manager, 2, 999, -10, 0, 0, 300, 200));
    window = chos_wm_get_const(&manager, 2);
    assert(window != NULL && window->x == 300 && window->y == 0);
    assert(!chos_wm_move(&manager, 2, 0, 0, 2, 0, 1, 1));
    /* Maximize and restore preserve the exact pre-maximize geometry. */
    assert(chos_wm_toggle_maximized(&manager, 2, 0, 0, 800, 570));
    window = chos_wm_get_const(&manager, 2);
    assert(window != NULL && window->maximized && window->x == 0 && window->y == 0 &&
           window->width == 800 && window->height == 570);
    assert(chos_wm_toggle_maximized(&manager, 2, 0, 0, 800, 570));
    window = chos_wm_get_const(&manager, 2);
    assert(window != NULL && !window->maximized && window->x == 300 && window->y == 0 &&
           window->width == 220 && window->height == 160);
    assert(chos_wm_close(&manager, 2));
    assert(chos_wm_active_id(&manager) == 1);
    assert(chos_wm_get_const(&manager, 2) == NULL);
    assert(chos_wm_close(&manager, 1));
    assert(chos_wm_active_id(&manager) == CHOS_WM_NO_WINDOW);
    assert(chos_wm_open_count(&manager) == 0u);

    /* Capacity is bounded and failed opens do not corrupt existing entries. */
    for (int i = 0; i < (int)CHOS_WM_MAX_WINDOWS; ++i)
        assert(chos_wm_open(&manager, i, "Window", i, i, 10, 10));
    assert(!chos_wm_open(&manager, 99, "Overflow", 0, 0, 10, 10));
    assert(chos_wm_open_count(&manager) == CHOS_WM_MAX_WINDOWS);
    assert(chos_wm_hit_test(&manager, 0, 0) >= 0);
    /* Re-rank before the z-order counter can wrap, keeping the same top window. */
    {
        int top = chos_wm_active_id(&manager);
        manager.next_z_order = UINT32_MAX;
        assert(chos_wm_activate(&manager, top));
        assert(chos_wm_active_id(&manager) == top);
        window = chos_wm_topmost(&manager);
        assert(window != NULL && window->id == top);
        assert(chos_wm_hit_test(&manager, window->x, window->y) == top);
    }
    puts("window-manager: focus, z-order, hit-test, title-control hitboxes at 1x-3x, minimize/restore, move clamps, maximize/restore, close, capacity and z-order renormalization passed");
    return 0;
}
