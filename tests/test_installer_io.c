#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "chos/installer_io.h"
static unsigned char *image;static uint64_t sectors=524288,id=55;static int writes,flushes,mode;static uint64_t highest;
static int snapshot(uint64_t *i,uint64_t *n){*i=id;*n=sectors;return 1;}
static int rd(chos_block_device_t *d,uint64_t lba,uint32_t n,void *p){(void)d;assert(lba<524288&&n<=524288-lba);size_t bytes=(size_t)n*512;if(mode==3)bytes/=2;memcpy(p,image+lba*512,bytes);return mode!=4;}
static int wr(chos_block_device_t *d,uint64_t lba,uint32_t n,const void *p){(void)d;assert(lba<524288&&n<=524288-lba);writes++;if(lba+n>highest)highest=lba+n;if(mode==1)return 0;size_t bytes=(size_t)n*512;if(mode==2)bytes/=2;memcpy(image+lba*512,p,bytes);return 1;}
static int fl(chos_block_device_t *d){(void)d;flushes++;return mode!=5;}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);rewind(f);assert(length>24);unsigned char *payload=malloc((size_t)length);assert(payload&&fread(payload,1,(size_t)length,f)==(size_t)length);fclose(f);
 image=calloc(524288,512);assert(image);chos_block_device_t d={.name="DISPOSABLE TEST",.sector_count=524288,.context=0,.read=rd,.write=wr,.flush=fl,.ready=1};chos_install_job_t j;
 assert(chos_install_select(&j,&d,snapshot,payload,(size_t)length));assert(writes==0);
 assert(!chos_install_confirm(&j,"yes"));chos_install_step(&j);assert(writes==0);
 id++;assert(!chos_install_confirm(&j,CHOS_INSTALLER_CONFIRMATION));assert(writes==0);id--;
 assert(chos_install_select(&j,&d,snapshot,payload,(size_t)length));assert(chos_install_confirm(&j,CHOS_INSTALLER_CONFIRMATION));id++;chos_install_step(&j);assert(writes==0&&j.state==INSTALL_FAILED);id--;
 for(mode=1;mode<=5;mode++){
  memset(image,0xa5,524288ull*512);assert(chos_install_select(&j,&d,snapshot,payload,(size_t)length));assert(chos_install_confirm(&j,CHOS_INSTALLER_CONFIRMATION));chos_install_step(&j);assert(j.state==INSTALL_FAILED&&j.next_sector==0);
 }
 mode=0;assert(chos_install_select(&j,&d,snapshot,payload,(size_t)length));int before=writes;chos_install_cancel(&j);chos_install_step(&j);assert(writes==before&&j.state==INSTALL_CANCELLED);
 assert(chos_install_select(&j,&d,snapshot,payload,(size_t)length));assert(chos_install_confirm(&j,CHOS_INSTALLER_CONFIRMATION));chos_install_step(&j);assert(j.next_sector==128);chos_install_cancel(&j);before=writes;chos_install_step(&j);assert(writes==before);
 assert(chos_install_select(&j,&d,snapshot,payload,(size_t)length));assert(chos_install_confirm(&j,CHOS_INSTALLER_CONFIRMATION));d.ready=0;chos_install_step(&j);assert(j.state==INSTALL_FAILED);d.ready=1;
 payload[20]^=1;assert(!chos_install_select(&j,&d,snapshot,payload,(size_t)length));payload[20]^=1;
 assert(chos_install_select(&j,&d,snapshot,payload,(size_t)length));assert(chos_install_confirm(&j,CHOS_INSTALLER_CONFIRMATION));unsigned steps=0;while(j.state!=INSTALL_DONE&&j.state!=INSTALL_FAILED&&steps++<1100)chos_install_step(&j);assert(j.state==INSTALL_DONE);assert(highest==524288&&flushes>0);
 printf("installer I/O: confirmation/identity, pre-write revalidation, write/short-I/O/read/flush faults, removal, cancel, retry and complete verified install passed (%u steps)\n",steps);
 free(image);free(payload);return 0;
}
