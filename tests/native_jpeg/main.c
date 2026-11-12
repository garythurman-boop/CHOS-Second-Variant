#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <pthread.h>
#include <jpeglib.h>
#include <jerror.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#include "jpeg-cases.h"

#define CHECK(x) do { if (!(x)) { printf("[FAIL] native JPEG line %u\n", __LINE__); fflush(stdout); return 1; } } while (0)
enum { OK, BAD, LIMIT, NOMEM };
enum { BLOCKS = 512, WIDTH_MAX = 1024, HEIGHT_MAX = 768 };
#define INPUT_MAX (2u * 1024u * 1024u)
#define MEMORY_MAX (8u * 1024u * 1024u)
typedef struct { void *pointer; size_t size; int large; } Block;
typedef struct Decoder Decoder;
typedef struct {
    struct jpeg_source_mgr pub;
    const unsigned char *input;
    size_t length, available, pending_skip, fragment, refills;
} Source;
struct Decoder {
    struct jpeg_decompress_struct jpeg;
    struct jpeg_error_mgr errors;
    jmp_buf jump;
    Block blocks[BLOCKS];
    Source source;
    size_t budget, live, peak, allocations, fail_at;
    unsigned width, height, components;
    uint64_t hash;
    unsigned char *row;
    int error, message, arithmetic, arithmetic_buffered;
};
typedef struct {
    unsigned width, height, components;
    uint64_t hash;
    size_t peak, allocations, refills;
    int message, arithmetic, arithmetic_buffered;
} Observation;

extern void *__real_jpeg_get_small(j_common_ptr, size_t);
extern void *__real_jpeg_get_large(j_common_ptr, size_t);
extern void __real_jpeg_free_small(j_common_ptr, void *, size_t);
extern void __real_jpeg_free_large(j_common_ptr, void *, size_t);
static void *allocate(j_common_ptr cinfo, size_t size, int large) {
    Decoder *d = cinfo->client_data;
    ++d->allocations;
    if (d->fail_at && d->allocations == d->fail_at) { d->error = NOMEM; return NULL; }
    if (size > d->budget - d->live) { d->error = LIMIT; return NULL; }
    unsigned slot = 0;
    while (slot < BLOCKS && d->blocks[slot].pointer) ++slot;
    if (slot == BLOCKS) { d->error = LIMIT; return NULL; }
    void *pointer = large ? __real_jpeg_get_large(cinfo, size) : __real_jpeg_get_small(cinfo, size);
    if (!pointer) { d->error = NOMEM; return NULL; }
    d->blocks[slot] = (Block){pointer, size, large};
    d->live += size;
    if (d->live > d->peak) d->peak = d->live;
    return pointer;
}
static void release(j_common_ptr cinfo, void *pointer, size_t size, int large) {
    if (!pointer) return;
    Decoder *d = cinfo->client_data;
    unsigned slot = 0;
    while (slot < BLOCKS && d->blocks[slot].pointer != pointer) ++slot;
    if (slot == BLOCKS || d->blocks[slot].size != size || d->blocks[slot].large != large) abort();
    d->live -= size; d->blocks[slot] = (Block){0};
    if (large) __real_jpeg_free_large(cinfo, pointer, size);
    else __real_jpeg_free_small(cinfo, pointer, size);
}
void *__wrap_jpeg_get_small(j_common_ptr cinfo, size_t size) { return allocate(cinfo, size, 0); }
void *__wrap_jpeg_get_large(j_common_ptr cinfo, size_t size) { return allocate(cinfo, size, 1); }
void __wrap_jpeg_free_small(j_common_ptr cinfo, void *p, size_t size) { release(cinfo, p, size, 0); }
void __wrap_jpeg_free_large(j_common_ptr cinfo, void *p, size_t size) { release(cinfo, p, size, 1); }

static void fatal(j_common_ptr cinfo) {
    Decoder *d = cinfo->client_data;
    d->message = cinfo->err->msg_code;
    if (!d->error) d->error = d->message == JERR_OUT_OF_MEMORY ? NOMEM : BAD;
    longjmp(d->jump, 1);
}
static void emit(j_common_ptr cinfo, int level) {
    /* Strict reference-consumer policy: reject recoverable corrupt-image
     * warnings as well as fatal errors, including invented EOI on short input. */
    if (level < 0) fatal(cinfo);
}
static void init_source(j_decompress_ptr cinfo) { (void)cinfo; }
static boolean starved(j_decompress_ptr cinfo) { (void)cinfo; return FALSE; }
static void term_source(j_decompress_ptr cinfo) { (void)cinfo; }
static void skip_source(j_decompress_ptr cinfo, long requested) {
    if (requested <= 0) return;
    Source *s = (Source *)cinfo->src;
    size_t count = (size_t)requested;
    size_t immediate = count < s->pub.bytes_in_buffer ? count : s->pub.bytes_in_buffer;
    s->pub.next_input_byte += immediate; s->pub.bytes_in_buffer -= immediate;
    count -= immediate;
    if (count > SIZE_MAX - s->pending_skip) { Decoder *d = cinfo->client_data; d->error = BAD; longjmp(d->jump, 1); }
    s->pending_skip += count;
}
static void supply(Decoder *d) {
    Source *s = &d->source;
    if (s->available == s->length) { d->error = BAD; longjmp(d->jump, 1); }
    size_t count = s->length - s->available;
    if (count > s->fragment) count = s->fragment;
    s->available += count; ++s->refills;
    size_t position = (size_t)(s->pub.next_input_byte - s->input);
    if (position > s->available) abort();
    size_t skip = s->available - position;
    if (skip > s->pending_skip) skip = s->pending_skip;
    position += skip; s->pending_skip -= skip;
    s->pub.next_input_byte = s->input + position;
    s->pub.bytes_in_buffer = s->available - position;
}
static int decode(const struct JPEGCase *spec, const unsigned char *input, size_t length,
                  size_t fragment, size_t budget, size_t fail_at, Observation *o) {
    memset(o, 0, sizeof(*o));
    if (!input || length < 2 || length > INPUT_MAX || input[0] != 0xff || input[1] != 0xd8) return BAD;
    Decoder *d = calloc(1, sizeof(*d)); if (!d) return NOMEM;
    d->budget = budget; d->fail_at = fail_at;
    d->jpeg.err = jpeg_std_error(&d->errors); d->errors.error_exit = fatal; d->errors.emit_message = emit;
    d->jpeg.client_data = d;
    /* Mutable recovery state is heap-owned. No changed automatic state is
     * consulted after longjmp, no C++ objects or cross-thread jumps occur. */
    if (setjmp(d->jump)) goto done;
    jpeg_create_decompress(&d->jpeg);
    d->jpeg.mem->max_memory_to_use = (long)budget;
    if (fragment) {
        d->source = (Source){.input=input,.length=length,.fragment=fragment};
        d->source.pub = (struct jpeg_source_mgr){.init_source=init_source,.fill_input_buffer=starved,
            .skip_input_data=skip_source,.resync_to_restart=jpeg_resync_to_restart,.term_source=term_source,
            .bytes_in_buffer=0,.next_input_byte=input};
        d->jpeg.src = &d->source.pub;
    } else jpeg_mem_src(&d->jpeg, input, (unsigned long)length);
    while (jpeg_read_header(&d->jpeg, TRUE) == JPEG_SUSPENDED) supply(d);
    d->arithmetic = !!d->jpeg.arith_code;
    if (d->jpeg.data_precision != 8) { d->error = BAD; goto done; }
    if (!d->jpeg.image_width || !d->jpeg.image_height || d->jpeg.image_width > WIDTH_MAX || d->jpeg.image_height > HEIGHT_MAX) { d->error = LIMIT; goto done; }
    d->jpeg.out_color_space = spec->components == 1 ? JCS_GRAYSCALE : JCS_RGB;
    d->jpeg.dct_method = spec->dct_fast ? JDCT_IFAST : JDCT_ISLOW;
    d->jpeg.do_fancy_upsampling = spec->fancy;
    d->jpeg.scale_num = spec->scale_num; d->jpeg.scale_denom = spec->scale_denom;
    jpeg_calc_output_dimensions(&d->jpeg);
    if (!d->jpeg.output_width || !d->jpeg.output_height || d->jpeg.output_width > WIDTH_MAX || d->jpeg.output_height > HEIGHT_MAX) { d->error = LIMIT; goto done; }
    if (fragment && d->arithmetic) {
        /* The upstream arithmetic entropy decoder cannot suspend. Its header
         * still arrives incrementally, but defer entropy work until this
         * bounded input is complete. Do not invent EOI or drop the image. */
        while (d->source.available < d->source.length) supply(d);
        d->arithmetic_buffered = 1;
    }
    while (!jpeg_start_decompress(&d->jpeg)) supply(d);
    d->width = d->jpeg.output_width; d->height = d->jpeg.output_height; d->components = d->jpeg.output_components;
    if (d->components != spec->components || (d->components != 1 && d->components != 3)) { d->error = BAD; goto done; }
    d->row = malloc((size_t)d->width * d->components);
    if (!d->row) { d->error = NOMEM; goto done; }
    d->hash = UINT64_C(14695981039346656037);
    while (d->jpeg.output_scanline < d->jpeg.output_height) {
        JSAMPROW rows[] = {d->row};
        if (jpeg_read_scanlines(&d->jpeg, rows, 1) == 0) { supply(d); continue; }
        for (size_t x = 0; x < (size_t)d->width * d->components; ++x) { d->hash ^= d->row[x]; d->hash *= UINT64_C(1099511628211); }
    }
    while (!jpeg_finish_decompress(&d->jpeg)) supply(d);
    /* No synthesized terminator and no unconsumed trailing input is accepted. */
    uintptr_t begin = (uintptr_t)input, position = (uintptr_t)d->jpeg.src->next_input_byte;
    if (position < begin || position - begin != length ||
        (fragment && d->source.pending_skip)) d->error = BAD;
 done:
    jpeg_destroy_decompress(&d->jpeg);
    /* Upstream destruction must release every successful platform allocation,
     * including partial initialization and suspension/error unwind. */
    if (d->live) abort();
    for (unsigned i = 0; i < BLOCKS; ++i) if (d->blocks[i].pointer) abort();
    free(d->row);
    *o = (Observation){d->width,d->height,d->components,d->hash,d->peak,d->allocations,d->source.refills,d->message,d->arithmetic,d->arithmetic_buffered};
    int result = d->error; free(d); return result;
}
static int matches(const struct JPEGCase *spec, const Observation *o) {
    return o->width == spec->width && o->height == spec->height && o->components == spec->components && o->hash == spec->hash;
}
static int positive(void) {
    for (unsigned i = 0; i < sizeof(jpeg_cases) / sizeof(*jpeg_cases); ++i) {
        const struct JPEGCase *c = &jpeg_cases[i]; Observation o;
        int status = decode(c,c->data,c->length,0,MEMORY_MAX,0,&o);
        if (status || !matches(c,&o)) { printf("[DETAIL] JPEG %s status=%d message=%d %ux%ux%u hash=%016llx expected=%016llx\n",c->name,status,o.message,o.width,o.height,o.components,(unsigned long long)o.hash,(unsigned long long)c->hash); return 1; }
        printf("[JPEG PASS] %s %ux%ux%u peak=%lu hash=%016llx\n",c->name,o.width,o.height,o.components,(unsigned long)o.peak,(unsigned long long)o.hash);
    }
    puts("[ OK ] all native JPEG reference images passed"); return 0;
}
static int fragmented(void) {
    const size_t sizes[] = {1,7,64,1024};
    unsigned arithmetic_cases = 0, buffered_deliveries = 0;
    for (unsigned i = 0; i < sizeof(jpeg_cases) / sizeof(*jpeg_cases); ++i) {
        const struct JPEGCase *c = &jpeg_cases[i];
        for (unsigned j = 0; j < sizeof(sizes) / sizeof(*sizes); ++j) {
            Observation o; int status = decode(c,c->data,c->length,sizes[j],MEMORY_MAX,0,&o);
            if (status || !matches(c,&o) || o.refills != (c->length+sizes[j]-1)/sizes[j] || o.arithmetic_buffered != o.arithmetic) {
                printf("[DETAIL] JPEG fragmented %s size=%lu status=%d message=%d refills=%lu hash=%016llx\n",c->name,(unsigned long)sizes[j],status,o.message,(unsigned long)o.refills,(unsigned long long)o.hash);return 1;
            }
            if (o.arithmetic) { ++buffered_deliveries; if (!j) ++arithmetic_cases; }
        }
    }
    CHECK(arithmetic_cases && buffered_deliveries == arithmetic_cases * 4);
    printf("[ OK ] native JPEG arithmetic entropy uses bounded complete input (%u cases)\n",arithmetic_cases);
    return 0;
}
static int reject(const struct JPEGCase *c, const unsigned char *data, size_t length) {
    Observation o;
    return decode(c,data,length,0,MEMORY_MAX,0,&o) != OK && decode(c,data,length,7,MEMORY_MAX,0,&o) != OK;
}
static int negative(void) {
    const struct JPEGCase *c = &jpeg_cases[0]; Observation o;
    CHECK(!decode(c,c->data,c->length,0,MEMORY_MAX,0,&o)); size_t calls = o.allocations;
    CHECK(calls > 3 && calls < 128);
    for (size_t fail = 1; fail <= calls; ++fail) CHECK(decode(c,c->data,c->length,0,MEMORY_MAX,fail,&o) == NOMEM);
    CHECK(!decode(c,c->data,c->length,7,MEMORY_MAX,0,&o)); calls = o.allocations;
    CHECK(calls > 3 && calls < 128);
    for (size_t fail = 1; fail <= calls; ++fail) CHECK(decode(c,c->data,c->length,7,MEMORY_MAX,fail,&o) == NOMEM);
    CHECK(decode(c,c->data,c->length,0,64,0,&o) == LIMIT);
    CHECK(decode(c,c->data,c->length,7,64,0,&o) == LIMIT);
    const size_t cuts[] = {0,1,2,3,7,8,12,25,33,64};
    for (unsigned i = 0; i < sizeof(cuts)/sizeof(*cuts); ++i) CHECK(reject(c,c->data,cuts[i]));
    CHECK(reject(c,c->data,c->length-1)); CHECK(reject(c,c->data,c->length-2));
    CHECK(reject(c,c->data,INPUT_MAX+1));
    unsigned char *copy = malloc(c->length+1); CHECK(copy); memcpy(copy,c->data,c->length); copy[c->length] = 0;
    int rejected = reject(c,copy,c->length+1); free(copy); CHECK(rejected);
    size_t offset = 2;
    while (offset + 4 <= c->length) {
        CHECK(c->data[offset] == 0xff);
        unsigned marker = c->data[offset+1]; size_t length = ((size_t)c->data[offset+2]<<8)|c->data[offset+3];
        CHECK(length >= 2 && length <= c->length-offset-2);
        if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2 || marker == 0xc9 || marker == 0xca) break;
        offset += length+2;
    }
    CHECK(offset+9 <= c->length);
    copy = malloc(c->length); CHECK(copy); memcpy(copy,c->data,c->length);
    copy[offset+7] = 4; copy[offset+8] = 1;
    rejected = reject(c,copy,c->length); memcpy(copy,c->data,c->length);
    copy[offset+5] = 3; copy[offset+6] = 1;
    rejected &= reject(c,copy,c->length); free(copy); CHECK(rejected);
    puts("[ OK ] native JPEG fragmented input and failures passed"); return 0;
}
static void *worker(void *argument) {
    unsigned index = (uintptr_t)argument;
    for (unsigned n = 0; n < 12; ++n) {
        const struct JPEGCase *c = &jpeg_cases[(n*7+index) % (sizeof(jpeg_cases)/sizeof(*jpeg_cases))]; Observation o;
        if (decode(c,c->data,c->length,n%2 ? 7 : 0,MEMORY_MAX,0,&o) || !matches(c,&o)) return (void *)1;
        if (!reject(c,c->data,c->length-1)) return (void *)2;
    }
    return NULL;
}
int main(void) {
    CHECK(!positive()); CHECK(!fragmented()); CHECK(!negative());
    pthread_t threads[3]; pthread_attr_t attr;
    CHECK(!pthread_attr_init(&attr) && !pthread_attr_setstacksize(&attr,128*1024));
    for (unsigned i = 0; i < 3; ++i) CHECK(!pthread_create(&threads[i],&attr,worker,(void *)(uintptr_t)i));
    for (unsigned i = 0; i < 3; ++i) { void *result = NULL; CHECK(!pthread_join(threads[i],&result) && !result); }
    CHECK(!pthread_attr_destroy(&attr));
#ifdef __CHOS__
    chos_heap_stats_t stats; CHECK(!chos_libc_heap_info(&stats) && !stats.allocations && !stats.mapped_bytes);
#endif
    puts("[ OK ] actual JPEG workers and cleanup passed"); return 0;
}
