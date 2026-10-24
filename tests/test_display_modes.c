#include <assert.h>
#include <stdio.h>
#include "chos/display_modes.h"

int main(void) {
    chos_display_mode_t mode;
    assert(chos_display_mode_count() == CHOS_DISPLAY_MODE_COUNT);
    assert(chos_display_mode_get(0u, &mode));
    assert(mode.width == 800u && mode.height == 600u);
    assert(!chos_display_mode_get(CHOS_DISPLAY_MODE_COUNT, &mode));
    assert(!chos_display_mode_get(0u, NULL));
    assert(chos_display_mode_index(800u, 600u) == 0);
    assert(chos_display_mode_index(1920u, 1080u) >= 0);
    assert(chos_display_mode_index(2560u, 1440u) >= 0);
    assert(chos_display_mode_index(2560u, 1600u) >= 0);
    assert(chos_display_mode_index(2560u, 1920u) >= 0);
    assert(chos_display_mode_index(2048u, 1536u) >= 0);
    assert(chos_display_mode_index(2304u, 1440u) >= 0);
    assert(chos_display_mode_index(1920u, 1440u) >= 0);
    assert(chos_display_mode_index(1920u, 1200u) >= 0);
    assert(chos_display_mode_index(1920u, 768u) == -1);
    assert(chos_display_mode_aspect(0u)[0] == '4');
    assert(chos_display_mode_aspect(22u)[0] == '1');
    assert(chos_display_mode_aspect(23u)[0] == '4');
    puts("display modes: ok");
    return 0;
}
