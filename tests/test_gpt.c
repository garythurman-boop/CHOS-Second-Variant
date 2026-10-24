#include "chos/gpt.h"
#include "chos/chfs.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_SECTORS 4096u
#define TEST_BYTES ((size_t)TEST_SECTORS * CHOS_BLOCK_SECTOR_SIZE)
static uint8_t disk_bytes[TEST_BYTES];

typedef struct { uint8_t *bytes; uint64_t sectors; } memdisk_t;
static void put16(uint8_t *p, uint16_t x) { p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); }
static void put32(uint8_t *p, uint32_t x) { p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); p[2]=(uint8_t)(x>>16); p[3]=(uint8_t)(x>>24); }
static void put64(uint8_t *p, uint64_t x) { put32(p,(uint32_t)x); put32(p+4,(uint32_t)(x>>32)); }
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu; size_t i; unsigned b;
    for (i=0;i<n;i++) { c ^= p[i]; for (b=0;b<8;b++) c=(c>>1)^((c&1)?0xedb88320u:0u); }
    return ~c;
}
static int mem_read(chos_block_device_t *d, uint64_t lba, uint32_t n, void *out) {
    memdisk_t *m=(memdisk_t*)d->context;
    if (!m || lba > m->sectors || n > m->sectors-lba) return 0;
    memcpy(out,m->bytes+(size_t)lba*512u,(size_t)n*512u); return 1;
}
static int mem_write(chos_block_device_t *d, uint64_t lba, uint32_t n, const void *in) {
    memdisk_t *m=(memdisk_t*)d->context;
    if (!m || lba > m->sectors || n > m->sectors-lba) return 0;
    memcpy(m->bytes+(size_t)lba*512u,in,(size_t)n*512u); return 1;
}
static int mem_flush(chos_block_device_t *d) { return d != NULL; }
static memdisk_t memory_disk;
static chos_block_device_t device;

/* CHFS uses these path helpers from ROMFS. Keep this test focused on the disk
 * and filesystem boundary without pulling the ROMFS/application image into host test. */
int romfs_path_is_valid(const char *path) {
    size_t i, n = 0u;
    if (path == NULL || path[0] != '/' || path[1] == '\0') return 0;
    while (path[n] != '\0') { if (++n >= 48u) return 0; }
    if (path[n-1u] == '/') return 0;
    for (i = 0u; i < n; ++i) if ((unsigned char)path[i] < 0x20u) return 0;
    return 1;
}
int romfs_path_equal(const char *a, const char *b) {
    size_t i = 0u;
    if (!romfs_path_is_valid(a) || !romfs_path_is_valid(b)) return 0;
    while (a[i] && b[i]) {
        int x=(unsigned char)a[i], y=(unsigned char)b[i];
        if (x>='A' && x<='Z') x += 'a'-'A';
        if (y>='A' && y<='Z') y += 'a'-'A';
        if (x != y) return 0;
        ++i;
    }
    return a[i] == b[i];
}

static void write_partition(uint8_t *e, const uint8_t type[16], uint64_t first, uint64_t last,
                            const char *name, uint8_t guid_seed) {
    size_t i;
    memset(e,0,128);
    memcpy(e,type,16);
    for (i=0;i<16;i++) e[16+i]=(uint8_t)(guid_seed+i+1);
    put64(e+32,first); put64(e+40,last); put64(e+48,0);
    for (i=0; name[i] && i<36; ++i) put16(e+56+i*2,(uint16_t)(uint8_t)name[i]);
}
static void finish_gpt(void) {
    uint8_t *hdr=disk_bytes+512u;
    uint8_t *entries=disk_bytes+2u*512u;
    uint32_t crc=crc32(entries,128u*128u);
    put32(hdr+88,crc);
    put32(hdr+16,0u);
    put32(hdr+16,crc32(hdr,92u));
    uint8_t *backup=disk_bytes+(TEST_SECTORS-1u)*512u;
    memcpy(disk_bytes+(TEST_SECTORS-33u)*512u,entries,128u*128u);
    memcpy(backup,hdr,512u);
    put64(backup+24,TEST_SECTORS-1u);put64(backup+32,1u);
    put64(backup+72,TEST_SECTORS-33u);put32(backup+16,0u);
    put32(backup+16,crc32(backup,92u));
}
static void make_gpt(void) {
    uint8_t *mbr=disk_bytes;
    uint8_t *hdr=disk_bytes+512u;
    uint8_t *e=disk_bytes+2u*512u;
    memset(disk_bytes,0,TEST_BYTES);
    mbr[446+4]=0xee; put32(mbr+446+8,1u); put32(mbr+446+12,TEST_SECTORS-1u);
    mbr[510]=0x55; mbr[511]=0xaa;
    memcpy(hdr,"EFI PART",8); put32(hdr+8,0x00010000u); put32(hdr+12,92u);
    put64(hdr+24,1u); put64(hdr+32,TEST_SECTORS-1u);
    put64(hdr+40,34u); put64(hdr+48,TEST_SECTORS-34u);
    put64(hdr+72,2u); put32(hdr+80,128u); put32(hdr+84,128u);
    write_partition(e,chos_gpt_efi_system_type_guid,40u,200u,"EFI System",0x11u);
    write_partition(e+128,chos_gpt_chos_data_type_guid,256u,3500u,"CHOS Data",0x51u);
    finish_gpt();
    memory_disk.bytes=disk_bytes; memory_disk.sectors=TEST_SECTORS;
    device.name="memory-test-disk"; device.sector_count=TEST_SECTORS; device.context=&memory_disk;
    device.read=mem_read; device.write=mem_write; device.flush=mem_flush; device.ready=1;
}
static void test_valid_and_slice(void) {
    chos_gpt_partition_t parts[4]; size_t count=0;
    chos_block_device_t slice; chos_block_slice_context_t ctx;
    uint8_t a[512], b[512];
    memset(parts,0,sizeof(parts));
    assert(chos_gpt_read(&device,parts,4,&count)==CHOS_GPT_OK);
    assert(count==2);
    assert(chos_gpt_guid_equal(parts[0].type_guid,chos_gpt_efi_system_type_guid));
    assert(parts[0].first_lba==40 && parts[0].last_lba==200);
    assert(strcmp(parts[0].name,"EFI System")==0);
    assert(chos_gpt_guid_equal(parts[1].type_guid,chos_gpt_chos_data_type_guid));
    assert(strcmp(parts[1].name,"CHOS Data")==0);
    memset(a,0xa5,sizeof(a)); assert(block_write(&device,256,1,a));
    assert(chos_block_slice_init(&slice,&ctx,&device,256,3245,"chos-data"));
    memset(b,0,sizeof(b)); assert(block_read(&slice,0,1,b)); assert(memcmp(a,b,512)==0);
    assert(!block_read(&slice,3245,1,b));
    assert(!chos_block_slice_init(&slice,&ctx,&device,TEST_SECTORS-2,4,"bad"));
    /* An actual CHFS format/mount/write cycle through the slice must stay
     * inside the CHOS GPT partition and leave the GPT header intact. */
    {
        chos_chfs_t fs, remounted;
        char payload[] = "partition mount works";
        char result[64]; size_t got=0u;
        assert(chfs_format(&slice));
        assert(chfs_mount(&fs,&slice));
        assert(chfs_make_directory(&fs,"/state"));
        assert(chfs_write_file(&fs,"/state/hello.txt",payload,sizeof(payload)));
        assert(chfs_mount(&remounted,&slice));
        memset(result,0,sizeof(result));
        assert(chfs_read_file(&remounted,"/state/hello.txt",result,sizeof(result),&got));
        assert(got==sizeof(payload) && memcmp(result,payload,got)==0);
        assert(memcmp(disk_bytes+512,"EFI PART",8)==0);
    }
}
static void test_header_crc(void) {
    chos_gpt_partition_t p[4]; size_t n;
    disk_bytes[512+40] ^= 1u;
    assert(chos_gpt_read(&device,p,4,&n)==CHOS_GPT_ERR_HEADER_CRC);
    disk_bytes[512+40] ^= 1u;
}
static void test_entry_crc(void) {
    chos_gpt_partition_t p[4]; size_t n;
    disk_bytes[2*512+32] ^= 1u;
    assert(chos_gpt_read(&device,p,4,&n)==CHOS_GPT_ERR_ENTRIES_CRC);
    disk_bytes[2*512+32] ^= 1u;
}
static void test_ranges_and_overlap(void) {
    chos_gpt_partition_t p[4]; size_t n;
    uint8_t *entries=disk_bytes+2*512;
    put64(entries+128+32,150u); put64(entries+128+40,250u); finish_gpt();
    assert(chos_gpt_read(&device,p,4,&n)==CHOS_GPT_ERR_OVERLAP);
    put64(entries+128+32,4000u); put64(entries+128+40,4100u); finish_gpt();
    assert(chos_gpt_read(&device,p,4,&n)==CHOS_GPT_ERR_PARTITION_RANGE);
}
static void test_bad_mbr_and_capacity(void) {
    chos_gpt_partition_t p[1]; size_t n;
    disk_bytes[446+4]=0; assert(chos_gpt_read(&device,p,1,&n)==CHOS_GPT_ERR_MBR);
    disk_bytes[446+4]=0xee;
    make_gpt();
    assert(chos_gpt_read(&device,p,1,&n)==CHOS_GPT_ERR_CAPACITY);
    assert(n==1);
}
static void test_backup_integrity(void){
    chos_gpt_partition_t parts[4];size_t count;
    make_gpt();disk_bytes[(TEST_SECTORS-1u)*512u+16]^=1;
    assert(chos_gpt_read(&device,parts,4,&count)==CHOS_GPT_ERR_HEADER_CRC);
    make_gpt();disk_bytes[(TEST_SECTORS-33u)*512u+20]^=1;
    assert(chos_gpt_read(&device,parts,4,&count)==CHOS_GPT_ERR_ENTRIES_CRC);
    make_gpt();put64(disk_bytes+(TEST_SECTORS-1u)*512u+32,2);
    assert(chos_gpt_read(&device,parts,4,&count)==CHOS_GPT_ERR_HEADER);
    make_gpt();
}
int main(void) {
    test_backup_integrity();
    make_gpt(); test_valid_and_slice(); make_gpt(); test_header_crc();
    make_gpt(); test_entry_crc(); make_gpt(); test_ranges_and_overlap();
    make_gpt(); test_bad_mbr_and_capacity();
    puts("GPT parser and block slices: CRC validation, range/overlap rejection, safe partition views and no-write parsing passed");
    return 0;
}
