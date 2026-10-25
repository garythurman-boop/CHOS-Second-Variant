#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "chos/power.h"
#include "chos/blockdev.h"
static uint64_t now;
static unsigned calls,flushes,conduit,function;
static int mounted,flush_ok=1;
static chos_block_device_t disk;
uint64_t timer_counter(void){return now++;}
uint64_t timer_frequency_hz(void){return 1000;}
int storage_mounted(void){return mounted;}
chos_block_device_t *storage_device(void){return &disk;}
int storage_sync(void){++flushes;return flush_ok;}
void uart_puts(const char *s){(void)s;}
int64_t chos_arch_psci(unsigned c,uint32_t f){++calls;conduit=c;function=f;return -1;}
static int write_stub(chos_block_device_t *d,uint64_t s,uint32_t n,const void *b){(void)d;(void)s;(void)n;(void)b;return 1;}
int main(void){
 assert(chos_power_execute(CHOS_POWER_RESTART) && !calls);
 chos_power_set_conduit(2);mounted=1;disk.write=write_stub;flush_ok=0;
 assert(chos_power_execute(CHOS_POWER_RESTART) && !calls && flushes==1);
 flush_ok=1;assert(chos_power_execute(CHOS_POWER_RESTART));
 assert(calls==1 && conduit==2 && function==0x84000009u && flushes==2);
 now=0;chos_power_set_conduit(1);assert(chos_power_execute(CHOS_POWER_SHUTDOWN));
 assert(now>=5000 && calls==2 && conduit==1 && function==0x84000008u && flushes==4);
 disk.write=0;assert(chos_power_execute(CHOS_POWER_RESTART));assert(flushes==4);
 assert(chos_power_execute(99) && calls==3);
 puts("power transition policy tests passed");
}
