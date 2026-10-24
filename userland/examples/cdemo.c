#include <stddef.h>
#include "chos/user.h"

static const char greeting[] = "CHOS C application: CHCX v2 segments are active.\n";
static const char sleeping[] = "CHOS C application: timer sleep/wakeup works.\n";
static const char error[] = "CHOS C application: runtime or memory test failed.\n";
static const char note_path[] = "/DOCS/README.TXT";
static const char no_note[] = "CHOS C application: no optional storage note found.\n";
static volatile unsigned int bss_counter;
static volatile unsigned int initialized_counter = 0x43484f53u;

int main(int argc, char **argv) {
    long pid = chos_getpid();
    ++bss_counter;
    if (pid <= 0 || argc < 1 || argv == NULL || argv[0] == NULL ||
        (argc > 1 && chos_strcmp(argv[1], "argument-ok") != 0) ||
        bss_counter != 1u || initialized_counter != 0x43484f53u) {
        (void)chos_write(error, sizeof(error) - 1u);
        return 1;
    }
    ++initialized_counter;
    if (chos_write(greeting, sizeof(greeting) - 1u) < 0) return 2;
    {
        long fd = chos_open(note_path);
        if (fd >= 0) {
            char note[128];
            long n = chos_read((int)fd, note, sizeof(note));
            if (n < 0 || chos_close((int)fd) < 0) return 8;
            if (n > 0 && chos_write(note, (size_t)n) < 0) return 9;
        } else {
            (void)chos_write(no_note, sizeof(no_note) - 1u);
        }
    }
    if (argc > 1 && chos_write(argv[1], chos_strlen(argv[1])) < 0) return 6;
    if (argc > 1 && chos_write("\n", 1u) < 0) return 7;
    if (chos_sleep_ticks(2u) < 0) {
        (void)chos_write(error, sizeof(error) - 1u);
        return 3;
    }
    if (initialized_counter != 0x43484f54u || bss_counter != 1u) {
        (void)chos_write(error, sizeof(error) - 1u);
        return 4;
    }
    if (chos_write(sleeping, sizeof(sleeping) - 1u) < 0) return 5;
    (void)chos_yield();
    return 0;
}
