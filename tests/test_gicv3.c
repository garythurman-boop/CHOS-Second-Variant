#define CHOS_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/arch/aarch64/gicv3.c"
static uint64_t cpus[8];
static uint32_t control,group,enabled;
static int active,security_disabled,refuse_sre,refuse_eoi,stuck;
uint64_t chos_gic_mock_cpu_read(unsigned r) {
    if(r==CPU_IAR) {
        unsigned group_bit=security_disabled?2:1;
        if(!active && (control&group_bit) && (enabled&(1u<<30)) && cpus[CPU_IGRPEN]) {active=1;return 30;}
        return 1023;
    }
    return cpus[r];
}
void chos_gic_mock_cpu_write(unsigned r,uint64_t v) {
    if((r==CPU_SRE && refuse_sre) || (r==CPU_CTLR && refuse_eoi)) return;
    cpus[r]=v;
    if(r==CPU_EOIR && !(cpus[CPU_CTLR]&2)) active=0;
}
uint64_t chos_gic_mock_mmio_read(uintptr_t a,unsigned width) {
    (void)width;
    if(a==0x08000000)return control|(stuck?1u<<31:0);
    if(a==0x080a0008)return 1u<<4;
    if(a==0x080b0080)return group;
    return 0;
}
void chos_gic_mock_mmio_write(uintptr_t a,uint64_t v,unsigned width) {
    (void)width;
    if(a==0x08000000)control=(uint32_t)v;
    if(a==0x080b0080)group=(uint32_t)v;
    if(a==0x080b0100)enabled|=(uint32_t)v;
    if(a==0x080b0180)enabled&=~(uint32_t)v;
}
int chos_boot_info_mmio_safe(const chos_boot_info_t *b,uint64_t a,uint64_t n) {(void)b;(void)a;(void)n;return 1;}
static void reset(void) {memset(cpus,0,sizeof(cpus));cpus[CPU_CTLR]=2;control=group=enabled=0;active=refuse_sre=refuse_eoi=stuck=0;}
int main(void) {
    chos_boot_info_t b={0};chos_acpi_info_t a={.valid=1,.gic_version=3,.gic_distributor_base=0x08000000,.gic_redistributor_base=0x080a0000,.gic_redistributor_length=0x20000};
    for(security_disabled=0;security_disabled<=1;++security_disabled) {
        reset();assert(chos_gicv3_init(&b,&a,30));assert(!(cpus[CPU_CTLR]&2));
        assert(chos_gicv3_enable_timer_irq());
        for(unsigned i=0;i<5;++i){assert(chos_gicv3_acknowledge()==30);chos_gicv3_end(30);assert(!active);}
        chos_gicv3_disable_timer_irq();assert(chos_gicv3_acknowledge()==1023);
    }
    reset();refuse_sre=1;assert(!chos_gicv3_init(&b,&a,30));
    reset();refuse_eoi=1;assert(!chos_gicv3_init(&b,&a,30));
    reset();stuck=1;assert(!chos_gicv3_init(&b,&a,30));
    puts("GICv3: inherited split EOI, both security views, repeated PPI delivery, masking and bounded setup faults passed");
}
