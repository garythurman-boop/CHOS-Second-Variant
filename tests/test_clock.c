#define CHOS_HOST_TEST 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/firmware_time.h"
#include "../kernel/platform/clock.c"
static uint64_t mock_counter;
static unsigned mmio_reads;
uint64_t timer_counter(void) {return mock_counter;}
uint32_t chos_clock_mock_mmio(uintptr_t a) {assert(a==0x12340000);++mmio_reads;return 1792800000;}
int main(void) {
    uint64_t epoch;
    chos_firmware_time_t time={.year=1970,.month=1,.day=1};
    assert(chos_firmware_time_epoch(&time,&epoch) && epoch==0);
    time=(chos_firmware_time_t){.year=2000,.month=2,.day=29};
    assert(chos_firmware_time_epoch(&time,&epoch) && epoch==951782400);
    time.year=2100;assert(!chos_firmware_time_epoch(&time,&epoch));
    time=(chos_firmware_time_t){.year=2026,.month=10,.day=24,.hour=12,.timezone=120};
    assert(chos_firmware_time_epoch(&time,&epoch));uint64_t local=epoch;
    time.timezone=0;assert(chos_firmware_time_epoch(&time,&epoch) && epoch==local+7200);
    time.timezone=2047;assert(chos_firmware_time_epoch(&time,&local) && local==epoch);
    time.second=60;assert(!chos_firmware_time_epoch(&time,&epoch));time.second=0;
    time.day=32;assert(!chos_firmware_time_epoch(&time,&epoch));
    chos_boot_info_t boot={.flags=CHOS_BOOT_FLAG_CLOCK,.clock_unix_seconds=1792800000,.clock_counter=100,.clock_frequency=1000};
    mock_counter=100;assert(chos_clock_init_uefi(&boot));assert(platform_rtc_unix_seconds()==1792800000);
    mock_counter+=61000;assert(platform_rtc_unix_seconds()==1792800061 && !mmio_reads);
    boot.clock_counter=UINT64_MAX-999;mock_counter=1000;assert(chos_clock_init_uefi(&boot));
    assert(platform_rtc_unix_seconds()==1792800002); /* architectural counter wrap */
    boot.clock_frequency=0;assert(!chos_clock_init_uefi(&boot));assert(!platform_rtc_unix_seconds() && !mmio_reads);
    assert(chos_clock_use_pl031(0x12340000));assert(platform_rtc_unix_seconds()==1792800000 && mmio_reads==2);
    assert(!chos_clock_init_uefi(NULL));assert(!platform_rtc_unix_seconds() && mmio_reads==2);
    puts("Clock: EFI calendar/timezone bounds, counter progress/wrap, no IRQ dependency and no UEFI fixed-MMIO access passed");
}
