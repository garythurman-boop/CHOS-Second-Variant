/* Selected memory API qualification of original zlib, not a gzFile consumer. */
#include <zlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#include "zlib-cases.h"

#define CHECK(x) do { if (!(x)) { printf("[FAIL] native zlib line %u\n", __LINE__); fflush(stdout); return 1; } } while (0)
enum { OUTPUT_CAPACITY = 65536, BLOCK_CAPACITY = 32 };
typedef struct { void *pointer; size_t length; } Block;
typedef struct {
    Block blocks[BLOCK_CAPACITY];
    size_t calls, fail_at, live, peak;
} Allocation;
typedef struct { size_t consumed, produced, calls; int default_callbacks; } Observation;
typedef struct { unsigned streams, partial, rejected, failures; } Counts;

static voidpf allocate(voidpf opaque, uInt items, uInt size) {
    Allocation *a = opaque;
    ++a->calls;
    if (a->fail_at && a->calls == a->fail_at) return NULL;
    if (size && (size_t)items > SIZE_MAX / size) return NULL;
    size_t length = (size_t)items * size;
    if (!length) length = 1;
    unsigned slot = 0;
    while (slot < BLOCK_CAPACITY && a->blocks[slot].pointer) ++slot;
    if (slot == BLOCK_CAPACITY) abort();
    void *pointer = calloc(1, length);
    if (!pointer) return NULL;
    a->blocks[slot] = (Block){pointer, length};
    a->live += length;
    if (a->live > a->peak) a->peak = a->live;
    return pointer;
}
static void release(voidpf opaque, voidpf pointer) {
    if (!pointer) return;
    Allocation *a = opaque;
    unsigned slot = 0;
    while (slot < BLOCK_CAPACITY && a->blocks[slot].pointer != pointer) ++slot;
    if (slot == BLOCK_CAPACITY) abort();
    a->live -= a->blocks[slot].length;
    a->blocks[slot] = (Block){0};
    free(pointer);
}
static int ledger_empty(const Allocation *a) {
    if (a->live) return 0;
    for (unsigned i = 0; i < BLOCK_CAPACITY; ++i)
        if (a->blocks[i].pointer) return 0;
    return 1;
}
static uInt fragment(size_t remaining, size_t amount) {
    return (uInt)(remaining < amount ? remaining : amount);
}

/* A fresh availability window is presented every call. total_in/out account for
 * exactly consumed bytes, so partially consumed fragments are never discarded.
 * Exhausted input without STREAM_END is explicitly incomplete, not successful. */
static int expand(const unsigned char *input, size_t length, int bits,
                  unsigned char *output, size_t capacity,
                  size_t input_fragment, size_t output_fragment,
                  Allocation *allocator, Observation *seen) {
    z_stream stream = {0};
    memset(seen, 0, sizeof(*seen));
    if (allocator) { stream.zalloc = allocate; stream.zfree = release; stream.opaque = allocator; }
    int result = inflateInit2(&stream, bits);
    if (result != Z_OK) return result;
    seen->default_callbacks = stream.zalloc != NULL && stream.zfree != NULL;
    size_t limit = (length + capacity) * 3 + 32;
    for (size_t call = 0; call < limit; ++call) {
        stream.next_in = (Bytef *)(input + stream.total_in);
        stream.avail_in = fragment(length - stream.total_in, input_fragment);
        stream.next_out = output + stream.total_out;
        stream.avail_out = fragment(capacity - stream.total_out, output_fragment);
        uLong before_in = stream.total_in, before_out = stream.total_out;
        result = inflate(&stream, Z_NO_FLUSH);
        ++seen->calls;
        if (result != Z_OK && result != Z_BUF_ERROR) break;
        if (stream.total_out == capacity ||
            (stream.total_in == before_in && stream.total_out == before_out)) {
            result = Z_BUF_ERROR; break;
        }
    }
    seen->consumed = stream.total_in; seen->produced = stream.total_out;
    int ended = inflateEnd(&stream);
    if (ended != Z_OK) return ended;
    return result;
}
static int encode(const unsigned char *input, size_t length, int bits,
                  unsigned char *output, size_t capacity,
                  size_t input_fragment, size_t output_fragment,
                  Allocation *allocator, Observation *seen) {
    z_stream stream = {0};
    memset(seen, 0, sizeof(*seen));
    if (allocator) { stream.zalloc = allocate; stream.zfree = release; stream.opaque = allocator; }
    int result = deflateInit2(&stream, 6, Z_DEFLATED, bits, 8, Z_DEFAULT_STRATEGY);
    if (result != Z_OK) return result;
    seen->default_callbacks = stream.zalloc != NULL && stream.zfree != NULL;
    size_t limit = (length + capacity) * 3 + 32;
    for (size_t call = 0; call < limit; ++call) {
        stream.next_in = (Bytef *)(input + stream.total_in);
        stream.avail_in = fragment(length - stream.total_in, input_fragment);
        stream.next_out = output + stream.total_out;
        stream.avail_out = fragment(capacity - stream.total_out, output_fragment);
        uLong before_in = stream.total_in, before_out = stream.total_out;
        result = deflate(&stream, stream.total_in == length ? Z_FINISH : Z_NO_FLUSH);
        ++seen->calls;
        if (result != Z_OK && result != Z_BUF_ERROR) break;
        if (stream.total_out == capacity ||
            (stream.total_in == before_in && stream.total_out == before_out)) {
            result = Z_BUF_ERROR; break;
        }
    }
    seen->consumed = stream.total_in; seen->produced = stream.total_out;
    int ended = deflateEnd(&stream);
    /* Aborted compression can validly return DATA_ERROR from deflateEnd. */
    if (result == Z_STREAM_END && ended != Z_OK) return ended;
    return result;
}
static int matches(const struct ZlibCase *c, const unsigned char *output, const Observation *seen) {
    return seen->produced == c->plain_length && !memcmp(output, c->plain, c->plain_length);
}
static int vectors(Counts *count) {
    unsigned char *output = malloc(OUTPUT_CAPACITY), *compressed = malloc(OUTPUT_CAPACITY);
    CHECK(output && compressed);
    const size_t fragments[][2] = {{1,1}, {7,13}, {257,61}, {65536,65536}};
    for (unsigned i = 0; i < ZLIB_CASE_COUNT; ++i) {
        const struct ZlibCase *c = &zlib_cases[i];
        for (unsigned f = 0; f < sizeof(fragments)/sizeof(*fragments); ++f) {
            Observation decoded, encoded;
            CHECK(expand(c->encoded, c->encoded_length, c->window_bits, output, OUTPUT_CAPACITY,
                         fragments[f][0], fragments[f][1], NULL, &decoded) == Z_STREAM_END);
            CHECK(decoded.default_callbacks && decoded.consumed == c->encoded_length && matches(c, output, &decoded));
            ++count->streams; if (f < 3) ++count->partial;
            CHECK(encode(c->plain, c->plain_length, c->window_bits, compressed, OUTPUT_CAPACITY,
                         fragments[f][0], fragments[f][1], NULL, &encoded) == Z_STREAM_END);
            CHECK(encoded.default_callbacks && encoded.consumed == c->plain_length);
            CHECK(expand(compressed, encoded.produced, c->window_bits, output, OUTPUT_CAPACITY,
                         fragments[f][1], fragments[f][0], NULL, &decoded) == Z_STREAM_END);
            CHECK(decoded.consumed == encoded.produced && matches(c, output, &decoded));
            ++count->streams; if (f < 3) ++count->partial;
        }
    }
    free(compressed); free(output); return 0;
}
static int utilities(Counts *count) {
    unsigned char *compressed = malloc(OUTPUT_CAPACITY), *plain = malloc(OUTPUT_CAPACITY);
    CHECK(compressed && plain);
    for (unsigned i = 0; i < ZLIB_CASE_COUNT; i += 3) {
        const struct ZlibCase *c = &zlib_cases[i];
        uLongf encoded = OUTPUT_CAPACITY, decoded = OUTPUT_CAPACITY;
        CHECK(compressBound(c->plain_length) <= OUTPUT_CAPACITY);
        CHECK(compress2(compressed, &encoded, c->plain, c->plain_length, Z_BEST_COMPRESSION) == Z_OK);
        uLong consumed = encoded;
        CHECK(uncompress2(plain, &decoded, compressed, &consumed) == Z_OK);
        CHECK(decoded == c->plain_length && consumed == encoded && !memcmp(plain, c->plain, decoded));
        decoded = OUTPUT_CAPACITY;
        CHECK(uncompress(plain, &decoded, compressed, encoded) == Z_OK && decoded == c->plain_length);
        encoded = OUTPUT_CAPACITY;
        CHECK(compress(compressed, &encoded, c->plain, c->plain_length) == Z_OK);
        decoded = OUTPUT_CAPACITY;
        CHECK(uncompress(plain, &decoded, compressed, encoded) == Z_OK && decoded == c->plain_length);
        CHECK(!memcmp(plain, c->plain, decoded));
        ++count->streams;
    }
    uLongf small = 1;
    CHECK(compress2(compressed, &small, zlib_cases[6].plain, zlib_cases[6].plain_length, 6) == Z_BUF_ERROR);
    small = 1;
    CHECK(uncompress(plain, &small, zlib_cases[7].encoded, zlib_cases[7].encoded_length) == Z_BUF_ERROR);
    count->rejected += 2;
    free(compressed); free(plain); return 0;
}
static int malformed(Counts *count) {
    unsigned char *output = malloc(OUTPUT_CAPACITY), *copy = malloc(OUTPUT_CAPACITY);
    CHECK(output && copy);
    for (unsigned i = 0; i < ZLIB_CASE_COUNT; ++i) {
        const struct ZlibCase *c = &zlib_cases[i]; Observation seen;
        const size_t cuts[] = {0, 1, c->encoded_length / 2, c->encoded_length - 1};
        for (unsigned n = 0; n < sizeof(cuts)/sizeof(*cuts); ++n) {
            CHECK(expand(c->encoded, cuts[n], c->window_bits, output, OUTPUT_CAPACITY, 7, 13, NULL, &seen) != Z_STREAM_END);
            ++count->rejected;
        }
        if (c->window_bits > 0) {
            memcpy(copy, c->encoded, c->encoded_length);
            /* Adler32 for zlib and CRC32 for gzip. raw deliberately has none. */
            copy[c->encoded_length - (c->window_bits == 31 ? 8 : 4)] ^= 1;
            CHECK(expand(copy, c->encoded_length, c->window_bits, output, OUTPUT_CAPACITY, 7, 13, NULL, &seen) == Z_DATA_ERROR);
            ++count->rejected;
            CHECK(expand(c->encoded, c->encoded_length, 47, output, OUTPUT_CAPACITY, 1, 7, NULL, &seen) == Z_STREAM_END);
            CHECK(matches(c, output, &seen)); ++count->streams;
        }
        /* Explicit wrappers reject the other wrapped representation. A raw
         * decoder cannot promise rejection of every arbitrary byte sequence. */
        if (c->window_bits == 15 || c->window_bits == 31) {
            int other = c->window_bits == 15 ? 31 : 15;
            CHECK(expand(c->encoded, c->encoded_length, other, output, OUTPUT_CAPACITY, 1, 7, NULL, &seen) == Z_DATA_ERROR);
            ++count->rejected;
        }
        memcpy(copy, c->encoded, c->encoded_length); copy[c->encoded_length] = 0x5a;
        CHECK(expand(copy, c->encoded_length + 1, c->window_bits, output, OUTPUT_CAPACITY, 7, 13, NULL, &seen) == Z_STREAM_END);
        CHECK(seen.consumed == c->encoded_length && matches(c, output, &seen));
        ++count->rejected; /* Consumer's exact-input policy rejects that tail. */
    }
    free(copy); free(output); return 0;
}
static int allocation_failures(Counts *count) {
    unsigned char *output = malloc(OUTPUT_CAPACITY);
    CHECK(output);
    for (unsigned i = 6; i < 9; ++i) {
        const struct ZlibCase *c = &zlib_cases[i];
        Observation seen; Allocation ledger = {0};
        CHECK(expand(c->encoded, c->encoded_length, c->window_bits, output, OUTPUT_CAPACITY, 7, 13, &ledger, &seen) == Z_STREAM_END);
        CHECK(matches(c, output, &seen) && ledger_empty(&ledger));
        size_t calls = ledger.calls;
        CHECK(calls >= 2 && calls < BLOCK_CAPACITY);
        for (size_t fail = 1; fail <= calls; ++fail) {
            memset(&ledger, 0, sizeof(ledger)); ledger.fail_at = fail;
            CHECK(expand(c->encoded, c->encoded_length, c->window_bits, output, OUTPUT_CAPACITY, 7, 13, &ledger, &seen) == Z_MEM_ERROR);
            CHECK(ledger_empty(&ledger)); ++count->failures;
        }
        memset(&ledger, 0, sizeof(ledger));
        CHECK(encode(c->plain, c->plain_length, c->window_bits, output, OUTPUT_CAPACITY, 7, 13, &ledger, &seen) == Z_STREAM_END);
        CHECK(ledger_empty(&ledger)); calls = ledger.calls;
        CHECK(calls >= 5 && calls < BLOCK_CAPACITY);
        for (size_t fail = 1; fail <= calls; ++fail) {
            memset(&ledger, 0, sizeof(ledger)); ledger.fail_at = fail;
            CHECK(encode(c->plain, c->plain_length, c->window_bits, output, OUTPUT_CAPACITY, 7, 13, &ledger, &seen) == Z_MEM_ERROR);
            CHECK(ledger_empty(&ledger)); ++count->failures;
        }
    }
    free(output); return 0;
}
static int resets(void) {
    unsigned char *compressed = malloc(OUTPUT_CAPACITY), *plain = malloc(OUTPUT_CAPACITY);
    CHECK(compressed && plain);
    z_stream encoder = {0}, decoder = {0};
    CHECK(deflateInit(&encoder, 6) == Z_OK && inflateInit(&decoder) == Z_OK);
    for (unsigned n = 0; n < 3; ++n) {
        const struct ZlibCase *c = &zlib_cases[n * 3];
        encoder.next_in = (Bytef *)c->plain; encoder.avail_in = c->plain_length;
        encoder.next_out = compressed; encoder.avail_out = OUTPUT_CAPACITY;
        CHECK(deflate(&encoder, Z_FINISH) == Z_STREAM_END);
        decoder.next_in = compressed; decoder.avail_in = encoder.total_out;
        decoder.next_out = plain; decoder.avail_out = OUTPUT_CAPACITY;
        CHECK(inflate(&decoder, Z_FINISH) == Z_STREAM_END);
        CHECK(decoder.total_out == c->plain_length && !memcmp(plain, c->plain, c->plain_length));
        CHECK(deflateReset(&encoder) == Z_OK && inflateReset(&decoder) == Z_OK);
    }
    CHECK(deflateEnd(&encoder) == Z_OK && inflateEnd(&decoder) == Z_OK);
    free(plain); free(compressed); return 0;
}
static void *worker(void *opaque) {
    unsigned index = (uintptr_t)opaque;
    unsigned char *plain = malloc(OUTPUT_CAPACITY), *compressed = malloc(OUTPUT_CAPACITY);
    if (!plain || !compressed) { free(plain); free(compressed); return (void *)1; }
    int bad = 0;
    for (unsigned n = 0; n < 12 && !bad; ++n) {
        const struct ZlibCase *c = &zlib_cases[(index + n * 5) % ZLIB_CASE_COUNT];
        Observation seen, encoded;
        bad = expand(c->encoded, c->encoded_length, c->window_bits, plain, OUTPUT_CAPACITY, 7, 29, NULL, &seen) != Z_STREAM_END || !matches(c, plain, &seen);
        if (!bad) bad = encode(c->plain, c->plain_length, c->window_bits, compressed, OUTPUT_CAPACITY, 31, 17, NULL, &encoded) != Z_STREAM_END;
        if (!bad) bad = expand(compressed, encoded.produced, c->window_bits, plain, OUTPUT_CAPACITY, 13, 23, NULL, &seen) != Z_STREAM_END || !matches(c, plain, &seen);
        if (!bad) bad = expand(c->encoded, c->encoded_length - 1, c->window_bits, plain, OUTPUT_CAPACITY, 7, 29, NULL, &seen) == Z_STREAM_END;
    }
    free(compressed); free(plain); return (void *)(uintptr_t)bad;
}
int main(void) {
    CHECK(!strcmp(zlibVersion(), ZLIB_VERSION));
    Counts count = {0};
    CHECK(!vectors(&count)); CHECK(!utilities(&count)); CHECK(!malformed(&count));
    CHECK(!allocation_failures(&count)); CHECK(!resets());
    pthread_t workers[3]; pthread_attr_t attr;
    CHECK(!pthread_attr_init(&attr) && !pthread_attr_setstacksize(&attr, 128 * 1024));
    for (unsigned i = 0; i < 3; ++i) CHECK(!pthread_create(&workers[i], &attr, worker, (void *)(uintptr_t)i));
    for (unsigned i = 0; i < 3; ++i) { void *result = NULL; CHECK(!pthread_join(workers[i], &result) && !result); }
    CHECK(!pthread_attr_destroy(&attr));
#ifdef __CHOS__
    chos_heap_stats_t stats;
    CHECK(!chos_libc_heap_info(&stats) && !stats.allocations && !stats.mapped_bytes);
#endif
    printf("ZLIB_NATIVE_PASS version=%s formats=3 vectors=%u streams=%u partial=%u rejected=%u allocation_failures=%u workers=3 worker_rounds=36 live=0 mapped=0\n",
           zlibVersion(), (unsigned)ZLIB_CASE_COUNT, count.streams, count.partial, count.rejected, count.failures);
    puts("[ OK ] native zlib memory streams, failures and workers passed");
    return 0;
}
