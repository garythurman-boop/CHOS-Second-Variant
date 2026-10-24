#include <stddef.h>
#include "chos/user.h"

static const char started[] = "CHOS C runtime: async demo started\n";
static const char resumed[] = "CHOS C runtime: woke after timer sleep\n";
static const char failed[] = "CHOS C runtime: syscall failed\n";

int main(void) {
    long pid = chos_getpid();
    unsigned int step;
    if (pid <= 0) {
        (void)chos_write(failed, sizeof(failed) - 1u);
        chos_exit(1);
    }
    (void)chos_write(started, sizeof(started) - 1u);
    for (step = 0u; step < 3u; ++step) {
        if (chos_sleep_ticks(3u) < 0) {
            (void)chos_write(failed, sizeof(failed) - 1u);
            chos_exit(2);
        }
        (void)chos_write(resumed, sizeof(resumed) - 1u);
        (void)chos_yield();
    }
    chos_exit(0);
}
