#include <assert.h>
#include <stdio.h>
#include "chos/virtio_gpu.h"

int main(void) {
    const chos_gpu_rect_t cursor_old = {100u, 100u, 24u, 32u};
    const chos_gpu_rect_t cursor_near = {108u, 104u, 24u, 32u};
    const chos_gpu_rect_t cursor_far = {2400u, 1700u, 24u, 32u};
    const chos_gpu_rect_t edge_old = {100u, 100u, 800u, 6u};
    const chos_gpu_rect_t edge_near = {112u, 108u, 800u, 6u};
    const chos_gpu_rect_t edge_far = {112u, 900u, 800u, 6u};
    assert(chos_gpu_rect_pair_should_coalesce(cursor_old, cursor_near));
    assert(!chos_gpu_rect_pair_should_coalesce(cursor_old, cursor_far));
    assert(chos_gpu_rect_pair_should_coalesce(edge_old, edge_near));
    assert(!chos_gpu_rect_pair_should_coalesce(edge_old, edge_far));
    puts("GPU damage: nearby dirty regions coalesce; long drag gaps use narrow independent flushes");
    return 0;
}
