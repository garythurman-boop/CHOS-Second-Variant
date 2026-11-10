#include <png.h>
#include <zlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#include "png-cases.h"
#define CHECK(x) do{if(!(x)){printf("[FAIL] native PNG line %u\n",__LINE__);fflush(stdout);return 1;}}while(0)
enum {OK,BAD,LIMIT,NOMEM};
enum {BLOCKS=1024,WIDTH_MAX=1024,HEIGHT_MAX=768};
#define INPUT_MAX (2u*1024u*1024u)
#define MEMORY_MAX (8u*1024u*1024u)
typedef struct {void *pointer;size_t size;} Block;
typedef struct {
    jmp_buf jump;
    png_structp png;png_infop info;
    const unsigned char *input;size_t length,position;
    Block blocks[BLOCKS];size_t live,peak,budget,allocations,fail_at;
    int error,complete;
    size_t trailing;
    png_bytepp rows;
    unsigned width,height;
    uint64_t hash;
} Decoder;
typedef struct {unsigned width,height;uint64_t hash;size_t peak,allocations;} Observation;
static void error(png_structp png,png_const_charp message){
    (void)message;Decoder *d=png_get_error_ptr(png);if(!d->error)d->error=BAD;longjmp(d->jump,1);
}
/* Strict untrusted-image fixture policy. In particular upstream can issue an
 * ordinary warning after the final row on a damaged progressive zlib stream;
 * png_set_benign_errors alone does not cover that branch. */
static void warning(png_structp png,png_const_charp message){error(png,message);}
static png_voidp allocate(png_structp png,png_alloc_size_t size){
    Decoder *d=png_get_mem_ptr(png);++d->allocations;
    if(d->fail_at&&d->allocations==d->fail_at){d->error=NOMEM;return NULL;}
    if(!size)size=1;
    if(size>d->budget-d->live){d->error=LIMIT;return NULL;}
    unsigned slot=0;while(slot<BLOCKS&&d->blocks[slot].pointer)++slot;
    if(slot==BLOCKS){d->error=LIMIT;return NULL;}
    void *p=malloc(size);if(!p){d->error=NOMEM;return NULL;}
    d->blocks[slot]=(Block){p,size};d->live+=size;if(d->live>d->peak)d->peak=d->live;
    return p;
}
static void release(png_structp png,png_voidp pointer){
    if(!pointer)return;Decoder *d=png_get_mem_ptr(png);
    unsigned i=0;while(i<BLOCKS&&d->blocks[i].pointer!=pointer)++i;
    /* Unknown frees are internal violations, never successful recovery. */
    if(i==BLOCKS)abort();d->live-=d->blocks[i].size;d->blocks[i]=(Block){0};free(pointer);
}
static void read_bytes(png_structp png,png_bytep target,png_size_t count){
    Decoder *d=png_get_io_ptr(png);
    if(count>d->length-d->position){d->error=BAD;png_error(png,"bounded input exhausted");}
    memcpy(target,d->input+d->position,count);d->position+=count;
}
static void prepare_rows(Decoder *d){
    d->width=png_get_image_width(d->png,d->info);d->height=png_get_image_height(d->png,d->info);
    if(!d->width||!d->height||d->width>WIDTH_MAX||d->height>HEIGHT_MAX){d->error=LIMIT;png_error(d->png,"dimension limit");}
    unsigned bits=png_get_bit_depth(d->png,d->info),type=png_get_color_type(d->png,d->info);
    int transparency=png_get_valid(d->png,d->info,PNG_INFO_tRNS);
    if(type==PNG_COLOR_TYPE_PALETTE)png_set_palette_to_rgb(d->png);
    if(type==PNG_COLOR_TYPE_GRAY&&bits<8)png_set_expand_gray_1_2_4_to_8(d->png);
    if(transparency)png_set_tRNS_to_alpha(d->png);
    if(bits==16)png_set_scale_16(d->png);
    if(type==PNG_COLOR_TYPE_GRAY||type==PNG_COLOR_TYPE_GRAY_ALPHA)png_set_gray_to_rgb(d->png);
    if(!(type&PNG_COLOR_MASK_ALPHA)&&!transparency)png_set_add_alpha(d->png,255,PNG_FILLER_AFTER);
    (void)png_set_interlace_handling(d->png);png_read_update_info(d->png,d->info);
    if(png_get_bit_depth(d->png,d->info)!=8||png_get_channels(d->png,d->info)!=4||png_get_rowbytes(d->png,d->info)!=(size_t)d->width*4)png_error(d->png,"output format");
    d->rows=png_malloc(d->png,d->height*sizeof(*d->rows));memset(d->rows,0,d->height*sizeof(*d->rows));
    for(unsigned y=0;y<d->height;++y){d->rows[y]=png_malloc(d->png,d->width*4);memset(d->rows[y],0,d->width*4);}
}
static void progressive_info(png_structp png,png_infop info){
    Decoder *d=png_get_progressive_ptr(png);if(info!=d->info||d->rows)png_error(png,"duplicate info");prepare_rows(d);
}
static void progressive_row(png_structp png,png_bytep row,png_uint_32 number,int pass){
    Decoder *d=png_get_progressive_ptr(png);(void)pass;
    if(!d->rows||number>=d->height)png_error(png,"row out of range");
    png_progressive_combine_row(png,d->rows[number],row);
}
static void progressive_end(png_structp png,png_infop info){
    Decoder *d=png_get_progressive_ptr(png);(void)info;
    if(d->complete)png_error(png,"duplicate image end");d->complete=1;
    d->trailing=png_process_data_pause(png,0);
}
static int decode_mode(const unsigned char *input,size_t length,size_t budget,size_t fail_at,Observation *observed,size_t fragment){
    memset(observed,0,sizeof(*observed));
    if(!input||length<8||length>INPUT_MAX||png_sig_cmp(input,0,8))return BAD;
    Decoder *d=calloc(1,sizeof(*d));if(!d)return NOMEM;
    d->input=input;d->length=length;d->budget=budget;d->fail_at=fail_at;
    /* Mutable recovery state is heap-owned; C nonlocal-jump rules cannot make
     * it indeterminate. No C++ objects or cross-thread jumps are involved. */
    if(setjmp(d->jump))goto done;
    d->png=png_create_read_struct_2(PNG_LIBPNG_VER_STRING,d,error,warning,d,allocate,release);
    if(!d->png){if(!d->error)d->error=NOMEM;goto done;}
    d->info=png_create_info_struct(d->png);if(!d->info){if(!d->error)d->error=NOMEM;goto done;}
    png_set_user_limits(d->png,WIDTH_MAX,HEIGHT_MAX);
    png_set_chunk_cache_max(d->png,32);png_set_chunk_malloc_max(d->png,65536);
    png_set_crc_action(d->png,PNG_CRC_ERROR_QUIT,PNG_CRC_ERROR_QUIT);
    /* Both benign-error and ordinary warning paths reject damaged streams. */
    png_set_benign_errors(d->png,0);
    if(fragment){
        png_set_progressive_read_fn(d->png,d,progressive_info,progressive_row,progressive_end);
        while(d->position<length&&!d->complete){
            size_t count=length-d->position;if(count>fragment)count=fragment;
            png_process_data(d->png,d->info,(png_bytep)(input+d->position),count);d->position+=count;
        }
        if(!d->complete||d->trailing||d->position!=length){d->error=BAD;goto done;}
    }else{
        png_set_read_fn(d->png,d,read_bytes);png_read_info(d->png,d->info);prepare_rows(d);
        png_read_image(d->png,d->rows);png_read_end(d->png,d->info);
        if(d->position!=length){d->error=BAD;goto done;}
    }
    d->hash=UINT64_C(14695981039346656037);
    for(unsigned y=0;y<d->height;++y)for(unsigned x=0;x<d->width*4;++x){d->hash^=d->rows[y][x];d->hash*=UINT64_C(1099511628211);}
 done:
    if(d->png)png_destroy_read_struct(&d->png,&d->info,NULL);
    for(unsigned i=0;i<BLOCKS;++i)if(d->blocks[i].pointer){d->live-=d->blocks[i].size;free(d->blocks[i].pointer);d->blocks[i]=(Block){0};}
    if(d->live)abort();
    int result=d->error;*observed=(Observation){d->width,d->height,d->hash,d->peak,d->allocations};free(d);return result;
}
static int decode(const unsigned char *input,size_t length,size_t budget,size_t fail_at,Observation *observed){
    return decode_mode(input,length,budget,fail_at,observed,0);
}
static int positive(void){
    for(unsigned i=0;i<sizeof(png_cases)/sizeof(*png_cases);++i){
        const struct PNGCase *c=&png_cases[i];Observation o;int status=decode(c->bytes,c->length,MEMORY_MAX,0,&o);
        if(status||o.width!=c->width||o.height!=c->height||o.hash!=c->hash){printf("[DETAIL] PNG %s status=%d %ux%u hash=%016llx expected=%016llx\n",c->name,status,o.width,o.height,(unsigned long long)o.hash,(unsigned long long)c->hash);return 1;}
        printf("[PNG PASS] %s %ux%u peak=%lu hash=%016llx\n",c->name,o.width,o.height,(unsigned long)o.peak,(unsigned long long)o.hash);
    }
    puts("[ OK ] all53 native PNG images match independent reference pixels");return 0;
}
static int progressive(void){
    const size_t fragments[]={1,7,64,1024};
    for(unsigned i=0;i<sizeof(png_cases)/sizeof(*png_cases);++i){
        const struct PNGCase *c=&png_cases[i];Observation o;
        /* Large image uses1024-byte delivery; small/interlaced cases exercise
         * every boundary size, including single-byte input callbacks. */
        unsigned first=i==52?3:0;
        for(unsigned f=first;f<4;++f){
            CHECK(!decode_mode(c->bytes,c->length,MEMORY_MAX,0,&o,fragments[f]));
            CHECK(o.width==c->width&&o.height==c->height&&o.hash==c->hash);
        }
    }
    const struct PNGCase *c=&png_cases[51];Observation o;
    CHECK(!decode_mode(c->bytes,c->length,MEMORY_MAX,0,&o,7));size_t calls=o.allocations;
    CHECK(calls>3&&calls<128);
    for(size_t fail=1;fail<=calls;++fail)CHECK(decode_mode(c->bytes,c->length,MEMORY_MAX,fail,&o,7)==NOMEM);
    CHECK(decode_mode(c->bytes,c->length-1,MEMORY_MAX,0,&o,7)==BAD);
    CHECK(decode_mode(c->bytes,c->length,64,0,&o,7)==LIMIT);
    unsigned char *copy=malloc(c->length+1);CHECK(copy);memcpy(copy,c->bytes,c->length);copy[c->length]=0;
    int rejected=decode_mode(copy,c->length+1,MEMORY_MAX,0,&o,7)==BAD;
    rejected&=decode_mode(copy,c->length+1,MEMORY_MAX,0,&o,c->length+1)==BAD;
    copy[29]^=1;rejected&=decode_mode(copy,c->length,MEMORY_MAX,0,&o,7)==BAD;free(copy);
    CHECK(rejected);
    puts("[ OK ] all53 progressive PNG images match reference under fragmented delivery and failures");return 0;
}
static uint32_t read32(const unsigned char *p){
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static void repair_crc(unsigned char *p,size_t offset,size_t length){
    uint32_t crc=crc32(0,p+offset+4,length+4);
    for(unsigned i=0;i<4;++i)p[offset+8+length+i]=(unsigned char)(crc>>(24-i*8));
}
static int damaged_payload(void){
    const struct PNGCase *c=&png_cases[51];Observation o;
    size_t offset=8,length=0;
    while(offset+12<=c->length){
        length=read32(c->bytes+offset);CHECK(length<=c->length-offset-12);
        if(!memcmp(c->bytes+offset+4,"IDAT",4))break;offset+=length+12;
    }
    CHECK(length>4&&offset+length+12<=c->length);
    unsigned char *copy=malloc(c->length);CHECK(copy);
    memcpy(copy,c->bytes,c->length);copy[offset+8+length-1]^=1;
    repair_crc(copy,offset,length);
    /* Input chunk CRC is valid: the actual inflater must detect its damaged
     * compressed-stream checksum, including under incremental delivery. */
    int rejected=decode_mode(copy,c->length,MEMORY_MAX,0,&o,0)==BAD;
    rejected&=decode_mode(copy,c->length,MEMORY_MAX,0,&o,7)==BAD;
    memcpy(copy,c->bytes,c->length);copy[20]=0;copy[21]=0;copy[22]=3;copy[23]=1;
    repair_crc(copy,8,13);
    rejected&=decode_mode(copy,c->length,MEMORY_MAX,0,&o,0)!=OK;
    rejected&=decode_mode(copy,c->length,MEMORY_MAX,0,&o,7)!=OK;
    free(copy);CHECK(rejected);
    CHECK(decode_mode(c->bytes,INPUT_MAX+1,MEMORY_MAX,0,&o,0)==BAD);
    CHECK(decode_mode(c->bytes,INPUT_MAX+1,MEMORY_MAX,0,&o,7)==BAD);
    puts("[ OK ] PNG valid-CRC damaged deflate and input/height limits rejected");return 0;
}
static int negative(void){
    const struct PNGCase *c=&png_cases[51];Observation o;
    CHECK(!decode(c->bytes,c->length,MEMORY_MAX,0,&o));size_t calls=o.allocations;
    CHECK(calls>3&&calls<128);
    for(size_t fail=1;fail<=calls;++fail)CHECK(decode(c->bytes,c->length,MEMORY_MAX,fail,&o)==NOMEM);
    CHECK(decode(c->bytes,c->length,64,0,&o)==LIMIT);
    const size_t cuts[]={0,1,7,8,12,25,33,64};
    for(unsigned i=0;i<sizeof(cuts)/sizeof(*cuts);++i)CHECK(decode(c->bytes,cuts[i],MEMORY_MAX,0,&o)!=OK);
    CHECK(decode(c->bytes,c->length-1,MEMORY_MAX,0,&o)!=OK);
    unsigned char *copy=malloc(c->length);CHECK(copy);memcpy(copy,c->bytes,c->length);
    copy[29]^=1;int rejected=decode(copy,c->length,MEMORY_MAX,0,&o)!=OK;free(copy);CHECK(rejected);
    /* Valid CRC on an out-of-policy IHDR: reject before image allocation. */
    copy=malloc(c->length);CHECK(copy);memcpy(copy,c->bytes,c->length);
    copy[16]=0;copy[17]=0;copy[18]=4;copy[19]=1;
    uint32_t crc=crc32(0,copy+12,17);for(unsigned i=0;i<4;++i)copy[29+i]=(unsigned char)(crc>>(24-i*8));
    rejected=decode(copy,c->length,MEMORY_MAX,0,&o)!=OK;free(copy);CHECK(rejected);
    puts("[ OK ] native PNG truncation,CRC,dimensions,budget and every allocation failure cleaned up");return 0;
}
static int utc(void){
    const time_t seconds[]={0,-1,1709164800,2147483647};
    const unsigned expected[][6]={{1970,1,1,0,0,0},{1969,12,31,23,59,59},{2024,2,29,0,0,0},{2038,1,19,3,14,7}};
    for(unsigned i=0;i<4;++i){png_time t;png_convert_from_time_t(&t,seconds[i]);CHECK(t.year==expected[i][0]&&t.month==expected[i][1]&&t.day==expected[i][2]&&t.hour==expected[i][3]&&t.minute==expected[i][4]&&t.second==expected[i][5]);}
    png_time t;png_convert_from_time_t(&t,INT64_MAX);CHECK(!t.year&&!t.month&&!t.day&&!t.hour&&!t.minute&&!t.second);return 0;
}
static void *worker(void *argument){
    unsigned index=(uintptr_t)argument;
    for(unsigned n=0;n<12;++n){const struct PNGCase *c=&png_cases[(n*7+index)%52];Observation o;
        if(decode_mode(c->bytes,c->length,MEMORY_MAX,0,&o,n%2?7:0)||o.hash!=c->hash)return (void*)1;
#ifdef __CHOS__
        /* The native UTC port uses a caller-owned buffer; the original host
         * convenience API is only tested sequentially, as it uses gmtime. */
        if(utc())return (void*)3;
#endif
        if(decode_mode(c->bytes,c->length-1,MEMORY_MAX,0,&o,n%2?7:0)==OK)return (void*)2;}
    return NULL;
}
int main(void){
    CHECK(!positive());CHECK(!negative());CHECK(!progressive());CHECK(!damaged_payload());CHECK(!utc());pthread_t threads[3];pthread_attr_t attr;
    CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,(void*)(uintptr_t)i));
    for(unsigned i=0;i<3;++i){void *result=NULL;CHECK(!pthread_join(threads[i],&result)&&!result);}
    CHECK(!pthread_attr_destroy(&attr));
#ifdef __CHOS__
    chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
#endif
    puts("[ OK ] actual PNG/zlib workers,UTC conversion and nonlocal error recovery passed");return 0;
}
