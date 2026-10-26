#include "chos/clock.h"
#include "chos/platform.h"
#include "chos/timer.h"
static struct {uint64_t epoch,counter,frequency;uintptr_t rtc;unsigned source;} clock_state;
#ifdef CHOS_HOST_TEST
extern uint32_t chos_clock_mock_mmio(uintptr_t address);
static uint32_t rtc_read(uintptr_t address) {return chos_clock_mock_mmio(address);}
#else
static uint32_t rtc_read(uintptr_t address) {return *(volatile uint32_t *)address;}
#endif
int chos_clock_init_uefi(const chos_boot_info_t *info) {
    clock_state.source=0;clock_state.rtc=0;
    if(!info || !(info->flags&CHOS_BOOT_FLAG_CLOCK) || (!info->clock_frequency || info->clock_frequency>UINT32_MAX) ||
       info->clock_unix_seconds>UINT64_C(253402300799))return 0;
    clock_state.epoch=info->clock_unix_seconds;clock_state.counter=info->clock_counter;
    clock_state.frequency=info->clock_frequency;clock_state.source=1;return 1;
}
int chos_clock_use_pl031(uintptr_t base) {
    if(!base || (base&3u))return 0;
    clock_state.rtc=base;clock_state.source=2;return 1;
}
const char *chos_clock_source(void) {
    return clock_state.source==1?"UEFI GetTime snapshot + architectural counter":
           clock_state.source==2?"FDT PL031 RTC":"unavailable";
}
uint64_t platform_rtc_unix_seconds(void) {
    if(clock_state.source==1) {
        uint64_t elapsed=(timer_counter()-clock_state.counter)/clock_state.frequency;
        if(elapsed>UINT64_C(253402300799)-clock_state.epoch)return 0;
        return clock_state.epoch+elapsed;
    }
    if(clock_state.source==2) {
        uint32_t a=rtc_read(clock_state.rtc),b=rtc_read(clock_state.rtc);
        uint32_t seconds=b>=a?b:rtc_read(clock_state.rtc);
        return seconds>=1577836800u && seconds<=4102444800u?seconds:0;
    }
    return 0;
}

void chos_clock_get_info(chos_clock_info_t *out) {
    if(!out)return;
    *out=(chos_clock_info_t){.version=CHOS_CLOCK_API_VERSION,.size_bytes=sizeof(*out)};
    uint64_t counter=timer_counter(),frequency=timer_frequency_hz();
    if(frequency && frequency<=UINT32_MAX){
        out->monotonic_seconds=counter/frequency;
        out->monotonic_nanoseconds=(uint32_t)((counter%frequency)*UINT64_C(1000000000)/frequency);
        out->flags|=CHOS_CLOCK_MONOTONIC_VALID;
    }
    if(clock_state.source==1){
        uint64_t elapsed=counter-clock_state.counter,seconds=elapsed/clock_state.frequency;
        if(seconds>UINT64_C(253402300799)-clock_state.epoch)return;
        out->realtime_seconds=clock_state.epoch+seconds;
        out->realtime_nanoseconds=(uint32_t)((elapsed%clock_state.frequency)*UINT64_C(1000000000)/clock_state.frequency);
    }else if(clock_state.source==2){
        out->realtime_seconds=platform_rtc_unix_seconds();if(!out->realtime_seconds)return;
    }else return;
    out->source=clock_state.source;out->flags|=CHOS_CLOCK_REALTIME_VALID;
}
