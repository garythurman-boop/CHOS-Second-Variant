#include "chos/installer_io.h"
#include "chos/chfs.h"
#include "chos/gpt.h"
#include "chos/display_modes.h"
/* One installer owner; permanent scratch never aliases a DMA buffer. */
static uint8_t expected[65536],actual[65536],entries[16384],header[512],mbr[512];
static uint32_t get32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static void zero(uint8_t *p,size_t n){for(size_t i=0;i<n;i++)p[i]=0;}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned b=0;b<8;b++)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
static int same(const uint8_t *a,const uint8_t *b,size_t n){for(size_t i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static int fail(chos_install_job_t *j,const char *e){j->state=INSTALL_FAILED;j->error=e;return 0;}
static int unchanged(chos_install_job_t *j){uint64_t id,n;return j->disk && j->disk->ready && j->snapshot(&id,&n) && id==j->plan.disk_identity && n==j->plan.disk_sector_count && n==j->disk->sector_count;}
static int valid_payload(const uint8_t *p,size_t n){
    static const uint8_t magic[]="CHOSINS1";
    if(!p||n<24||n>16u*1024u*1024u||!same(p,magic,8)||get32(p+8)!=512||get32(p+12)!=131072||crc(p+24,n-24)!=get32(p+20))return 0;
    uint32_t runs=get32(p+16),end=0;size_t off=24;
    if(!runs||runs>131072)return 0;
    for(uint32_t i=0;i<runs;i++){
        if(off>n||n-off<8)return 0;
        uint32_t start=get32(p+off),count=get32(p+off+4);off+=8;
        if(!count||count>128||start<end||start>=131072||count>131072-start||(size_t)count*512>n-off)return 0;
        end=start+count;off+=(size_t)count*512;
    }
    return off==n;
}
int chos_install_select(chos_install_job_t *j,chos_block_device_t *d,chos_install_snapshot_fn snapshot,const void *p,size_t n){
    uint64_t id,sectors;
    if(!j)return 0;
    j->state=INSTALL_IDLE;j->error="Select a target disk.";
    if(!d||!d->ready||!d->read||!d->write||!d->flush||!snapshot||!snapshot(&id,&sectors)||sectors!=d->sector_count)return fail(j,"Target identity, geometry or durable I/O unavailable.");
    if(!valid_payload(p,n))return fail(j,"Installer payload missing or corrupt; no disk writes performed.");
    if(!chos_installer_prepare_plan(id,sectors,512,&j->plan)||sectors>UINT32_MAX)return fail(j,"Target cannot hold the supported GPT/CHFS layout.");
    j->disk=d;j->snapshot=snapshot;j->payload=p;j->payload_size=n;j->next_sector=0;j->state=INSTALL_SELECTED;j->error="Ready for explicit erase confirmation.";return 1;
}
static int verified_write(chos_install_job_t *j,uint64_t lba,uint32_t n,const uint8_t *data){
    if(n>128)return fail(j,"I/O chunk exceeds installer workspace.");
    for(size_t i=0;i<(size_t)n*512;i++)actual[i]=(uint8_t)~data[i];
    if(!block_write(j->disk,lba,n,data)||!block_flush(j->disk)||!block_read(j->disk,lba,n,actual)||!same(data,actual,(size_t)n*512))return fail(j,"Write, flush or readback failed; installation is incomplete.");
    return 1;
}
static chos_install_job_t *formatting_job;
static int format_write(chos_block_device_t *device,uint64_t lba,uint32_t n,const void *data){
    chos_block_slice_context_t *ctx=device->context;
    if(!formatting_job||!ctx||lba>=ctx->sector_count||n>ctx->sector_count-lba)return 0;
    return verified_write(formatting_job,ctx->first_sector+lba,n,data);
}
static void make_entries(chos_install_job_t *j){
    zero(entries,sizeof(entries));
    const uint8_t *types[2]={chos_gpt_efi_system_type_guid,chos_gpt_chos_data_type_guid};
    uint64_t first[2]={j->plan.esp_first_lba,j->plan.data_first_lba},last[2]={j->plan.esp_last_lba,j->plan.data_last_lba};
    for(unsigned k=0;k<2;k++){
        uint8_t *e=entries+k*128;
        for(unsigned i=0;i<16;i++)e[i]=types[k][i];
        put64(e+16,j->plan.disk_identity);put64(e+24,k+1);e[22]=(e[22]&15)|64;e[24]=(e[24]&63)|128;
        put64(e+32,first[k]);put64(e+40,last[k]);
        const char *name=k?"CHOS Data":"EFI System";for(unsigned i=0;name[i];i++)e[56+i*2]=(uint8_t)name[i];
    }
}
static void make_header(chos_install_job_t *j,int backup){
    zero(header,512);const char *sig="EFI PART";for(unsigned i=0;i<8;i++)header[i]=(uint8_t)sig[i];
    put32(header+8,0x10000);put32(header+12,92);put64(header+24,backup?j->disk->sector_count-1:1);
    put64(header+32,backup?1:j->disk->sector_count-1);put64(header+40,34);put64(header+48,j->disk->sector_count-34);
    put64(header+56,j->plan.disk_identity);put64(header+64,0x43484f53494e5354ull);
    put64(header+72,backup?j->disk->sector_count-33:2);put32(header+80,128);put32(header+84,128);put32(header+88,crc(entries,sizeof(entries)));put32(header+16,crc(header,92));
}
int chos_install_confirm(chos_install_job_t *j,const char *text){
    if(!j||j->state!=INSTALL_SELECTED)return 0;
    if(!unchanged(j))return fail(j,"Target changed after selection; select it again.");
    if(!chos_installer_confirm_plan(&j->plan,j->plan.disk_identity,text))return 0;
    j->state=INSTALL_WRITING;j->error="Writing and verifying the EFI System Partition.";return 1;
}
static void esp_chunk(chos_install_job_t *j,uint32_t count){
    zero(expected,(size_t)count*512);size_t off=24;uint64_t begin=j->next_sector,end=begin+count;
    for(uint32_t i=0;i<get32(j->payload+16);i++){
        uint32_t start=get32(j->payload+off),n=get32(j->payload+off+4);off+=8;
        uint64_t left=start>begin?start:begin,right=(uint64_t)start+n<end?(uint64_t)start+n:end;
        if(left<right)for(size_t b=0;b<(right-left)*512;b++)expected[(left-begin)*512+b]=j->payload[off+(left-start)*512+b];
        off+=(size_t)n*512;
    }
}
typedef struct {uint32_t reserved,fat_size,spc,data_start,root,clusters;} install_fat_t;
static uint32_t get16(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8;}
static int fat_find(chos_install_job_t *j,const install_fat_t *f,uint32_t directory,const char name[11],uint32_t *cluster,uint32_t *size,uint8_t *attributes){
    uint8_t sector[512];
    for(unsigned budget=0;budget<256;budget++){
        if(directory<2||directory-2>=f->clusters)return 0;
        uint64_t lba=j->plan.esp_first_lba+f->data_start+(uint64_t)(directory-2)*f->spc;
        if(lba+f->spc>j->plan.esp_last_lba+1)return 0;
        for(uint32_t n=0;n<f->spc;n++){
            if(!block_read(j->disk,lba+n,1,sector))return 0;
            for(unsigned off=0;off<512;off+=32){
                if(sector[off]==0)return 0;
                if(sector[off]==0xe5||sector[off+11]==15)continue;
                if(same(sector+off,(const uint8_t *)name,11)){
                    *cluster=(get16(sector+off+20)<<16)|get16(sector+off+26);*size=get32(sector+off+28);*attributes=sector[off+11];return *cluster>=2&&*cluster-2<f->clusters;
                }
            }
        }
        uint64_t offset=(uint64_t)directory*4;
        if(offset/512>=f->fat_size||!block_read(j->disk,j->plan.esp_first_lba+f->reserved+offset/512,1,sector))return 0;
        directory=get32(sector+offset%512)&0x0fffffff;
        if(directory>=0x0ffffff8)return 0;
    }
    return 0;
}
static int verify_boot_files(chos_install_job_t *j){
    uint8_t sector[512],attr;uint32_t efi,boot,chos,file,size;
    if(!block_read(j->disk,j->plan.esp_first_lba,1,sector))return 0;
    install_fat_t f={get16(sector+14),get32(sector+36),sector[13],0,get32(sector+44),0};
    if(sector[510]!=0x55||sector[511]!=0xaa||get16(sector+11)!=512||sector[16]!=2||!f.spc||(f.spc&(f.spc-1))||f.spc>128||!f.reserved||!f.fat_size||f.fat_size>65535||get32(sector+32)!=131072)return 0;
    f.data_start=f.reserved+2*f.fat_size;if(f.data_start>=131072)return 0;f.clusters=(131072-f.data_start)/f.spc;
    if(!fat_find(j,&f,f.root,"EFI        ",&efi,&size,&attr)||!(attr&16)||
       !fat_find(j,&f,efi,"BOOT       ",&boot,&size,&attr)||!(attr&16)||
       !fat_find(j,&f,boot,"BOOTAA64EFI",&file,&size,&attr)||(attr&16)||size<512)return 0;
    if(!block_read(j->disk,j->plan.esp_first_lba+f.data_start+(uint64_t)(file-2)*f.spc,1,sector)||sector[0]!='M'||sector[1]!='Z')return 0;
    uint32_t pe=get32(sector+60);
    if(pe<64||pe>512-94||pe>size-94||sector[pe]!='P'||sector[pe+1]!='E'||sector[pe+2]!=0||sector[pe+3]!=0||get16(sector+pe+4)!=0xaa64||get16(sector+pe+24)!=0x20b||get16(sector+pe+92)!=10)return 0;
    if(!fat_find(j,&f,efi,"CHOS       ",&chos,&size,&attr)||!(attr&16)||
       !fat_find(j,&f,chos,"CHOS    ELF",&file,&size,&attr)||(attr&16)||size<64)return 0;
    if(!block_read(j->disk,j->plan.esp_first_lba+f.data_start+(uint64_t)(file-2)*f.spc,1,sector)||sector[0]!=127||sector[1]!='E'||sector[2]!='L'||sector[3]!='F'||sector[4]!=2||sector[5]!=1||get16(sector+18)!=183)return 0;
    return 1;
}
void chos_install_step(chos_install_job_t *j){
    if(!j)return;
    if(j->state==INSTALL_WRITING){
        /* Revalidate live identity immediately before the first write. No
         * selection token or cached geometry substitutes for this query. */
        if(!j->next_sector && (!unchanged(j)||!chos_installer_plan_is_confirmed(&j->plan,j->plan.disk_identity))){fail(j,"Target changed before writing.");return;}
        uint32_t count=128;if(count>131072-j->next_sector)count=(uint32_t)(131072-j->next_sector);
        esp_chunk(j,count);
        if(!verified_write(j,j->plan.esp_first_lba+j->next_sector,count,expected))return;
        j->next_sector+=count;
        if(j->next_sector==131072){j->state=INSTALL_FORMATTING;j->error="Initializing CHFS and writing both GPT copies.";}
    }else if(j->state==INSTALL_FORMATTING){
        chos_block_device_t slice;chos_block_slice_context_t ctx;
        if(!chos_block_slice_init(&slice,&ctx,j->disk,j->plan.data_first_lba,j->plan.data_last_lba-j->plan.data_first_lba+1,"install-data")){fail(j,"Data partition bounds invalid.");return;}
        formatting_job=j;slice.write=format_write;
        if(!chfs_format(&slice)){fail(j,"CHFS format failed; target is partially installed.");return;}
        make_entries(j);make_header(j,1);
        if(!verified_write(j,j->disk->sector_count-33,32,entries)||!verified_write(j,j->disk->sector_count-1,1,header))return;
        make_header(j,0);if(!verified_write(j,2,32,entries)||!verified_write(j,1,1,header))return;
        zero(mbr,512);mbr[450]=0xee;put32(mbr+454,1);put32(mbr+458,(uint32_t)(j->disk->sector_count-1));mbr[510]=0x55;mbr[511]=0xaa;
        if(!verified_write(j,0,1,mbr))return;
        j->state=INSTALL_VERIFYING;j->error="Checking GPT and the new filesystem.";
    }else if(j->state==INSTALL_VERIFYING){
        chos_gpt_partition_t parts[2];size_t count=0;static chos_chfs_t fs;
        chos_block_device_t slice;chos_block_slice_context_t ctx;
        if(!verify_boot_files(j)||chos_gpt_read(j->disk,parts,2,&count)!=CHOS_GPT_OK||count!=2||!chos_block_slice_init(&slice,&ctx,j->disk,j->plan.data_first_lba,j->plan.data_last_lba-j->plan.data_first_lba+1,"verify-data")||!chfs_mount(&fs,&slice)||!block_flush(j->disk)){fail(j,"Final GPT/CHFS verification failed.");return;}
        j->state=INSTALL_DONE;j->error="Installation verified. Shut down, detach ISO, then boot the disk.";
    }
}
void chos_install_cancel(chos_install_job_t *j){if(j&&j->state!=INSTALL_DONE){j->state=INSTALL_CANCELLED;j->error=j->next_sector?"Cancelled; target is incomplete. Select and confirm again to reinstall.":"Cancelled; no installer sectors were written.";}}

int chos_install_save_display(chos_block_device_t *disk,chos_install_snapshot_fn snapshot,uint64_t selected_identity,uint32_t width,uint32_t height){
    uint64_t identity,sectors;chos_gpt_partition_t parts[2];size_t count=0;
    if(chos_display_mode_index(width,height)<0)return 0;
    if(!disk||!disk->ready||!disk->write||!disk->flush||!snapshot||!snapshot(&identity,&sectors)||identity!=selected_identity||sectors!=disk->sector_count)return 0;
    if(chos_gpt_read(disk,parts,2,&count)!=CHOS_GPT_OK||count!=2||!chos_gpt_guid_equal(parts[0].type_guid,chos_gpt_efi_system_type_guid)||parts[0].last_lba-parts[0].first_lba+1!=131072)return 0;
    chos_install_job_t job={.disk=disk};job.plan.esp_first_lba=parts[0].first_lba;job.plan.esp_last_lba=parts[0].last_lba;
    if(!block_read(disk,parts[0].first_lba,1,actual))return 0;
    install_fat_t f={get16(actual+14),get32(actual+36),actual[13],0,get32(actual+44),0};
    if(get16(actual+11)!=512||actual[16]!=2||!f.spc||(f.spc&(f.spc-1))||f.spc>128||!f.reserved||!f.fat_size||f.fat_size>65535||get32(actual+32)!=131072)return 0;
    f.data_start=f.reserved+2*f.fat_size;if(f.data_start>=131072)return 0;f.clusters=(131072-f.data_start)/f.spc;
    uint32_t efi,chos,file,size;uint8_t attr;
    if(!fat_find(&job,&f,f.root,"EFI        ",&efi,&size,&attr)||!(attr&16)||!fat_find(&job,&f,efi,"CHOS       ",&chos,&size,&attr)||!(attr&16)||!fat_find(&job,&f,chos,"DISPLAY CFG",&file,&size,&attr)||(attr&16)||size!=64)return 0;
    uint64_t lba=parts[0].first_lba+f.data_start+(uint64_t)(file-2)*f.spc;
    if(lba>parts[0].last_lba||!block_read(disk,lba,1,expected))return 0;
    zero(expected,64);const char *magic="CHOSDSP1";for(unsigned i=0;i<8;i++)expected[i]=(uint8_t)magic[i];put32(expected+8,width);put32(expected+12,height);put32(expected+16,crc(expected,16));
    if(!snapshot(&identity,&sectors)||identity!=selected_identity||sectors!=disk->sector_count)return 0;
    return verified_write(&job,lba,1,expected);
}
