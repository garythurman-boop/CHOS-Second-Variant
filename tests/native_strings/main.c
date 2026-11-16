#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#define CHECK(x) do { if (!(x)) { printf("[FAIL] native strings line %u\n", __LINE__); return 1; } } while (0)

static int byte_cases(unsigned rotations)
{
    char input[256], set[256];
    for (unsigned rotation = 1; rotation <= rotations; ++rotation) {
        for (unsigned i = 0; i < 255; ++i)
            input[i] = (char)(1 + (i + rotation) % 255);
        input[255] = 0;
        for (unsigned n = 0; n <= 255; ++n) {
            for (unsigned i = 0; i < n; ++i)
                set[i] = (char)(1 + (i * 131 + rotation) % 255);
            set[n] = 0;
            const char *expected = NULL;
            for (unsigned i = 0; i < 255; ++i) {
                if (memchr(set, (unsigned char)input[i], n)) {
                    expected = input + i;
                    break;
                }
            }
            CHECK(strpbrk(input, set) == expected);
        }
    }
    return 0;
}

static int guards(void)
{
    long page = getpagesize();
    CHECK(page > 0);
    char *mapping = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED);
    CHECK(!mprotect(mapping + page, (size_t)page, PROT_NONE));
    char *end = mapping + page;
    end[-3] = (char)255; end[-2] = 'x'; end[-1] = 0;
    CHECK(strpbrk(end - 3, "x") == end - 2);
    CHECK(strpbrk(end - 3, "\xff") == end - 3);
    CHECK(!strpbrk(end - 3, "a"));
    CHECK(!strpbrk(end - 3, end - 1));
    CHECK(!strpbrk(end - 1, end - 3));
    char query[] = "abx";
    CHECK(strpbrk(query, end - 3) == query + 2);
    CHECK(strspn(end - 3, "\xffx") == 2);
    CHECK(strcspn(end - 3, "a") == 2);
    CHECK(!munmap(mapping, (size_t)page * 2));
    return 0;
}

static void *worker(void *unused)
{
    (void)unused;
    return (void *)(uintptr_t)(byte_cases(32) || guards());
}

int main(void)
{
    CHECK(!byte_cases(255) && !guards());
    CHECK(!strpbrk("", "abc") && !strpbrk("abc", ""));
    puts("[ OK ] native string byte cases and protected terminators passed");
    pthread_attr_t attr; pthread_t threads[3];
    CHECK(!pthread_attr_init(&attr));
    CHECK(!pthread_attr_setstacksize(&attr, 128 * 1024));
    for (unsigned i = 0; i < 3; ++i)
        CHECK(!pthread_create(&threads[i], &attr, worker, NULL));
    for (unsigned i = 0; i < 3; ++i) {
        void *result = NULL;
        CHECK(!pthread_join(threads[i], &result) && !result);
    }
    CHECK(!pthread_attr_destroy(&attr));
#ifdef __CHOS__
    chos_heap_stats_t stats;
    CHECK(!chos_libc_heap_info(&stats) && !stats.allocations && !stats.mapped_bytes);
#endif
    puts("[ OK ] native strings threads and mappings cleaned up");
    return 0;
}
