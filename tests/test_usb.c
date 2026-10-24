#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/usb.h"
#include "chos/xhci.h"
static unsigned events, keys, releases, buttons;
static int dx,dy,scroll,abs_x=-1,abs_y=-1;
void chos_input_key(uint16_t c,int down) {assert(c);++events;if(down)++keys;else ++releases;}
void chos_input_button(uint16_t c,int down) {assert(c>=0x110 && c<0x118);(void)down;++buttons;++events;}
void chos_input_relative(int32_t x,int32_t y) {dx+=x;dy+=y;++events;}
void chos_input_absolute(int32_t x,int32_t y){abs_x=x;abs_y=y;++events;}
void chos_input_scroll(int32_t w) {scroll+=w;++events;}
static const uint8_t keyboard[]={
0x05,1,0x09,6,0xa1,1,0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
0x95,1,0x75,8,0x81,1,0x95,6,0x75,8,0x15,0,0x25,0x65,0x19,0,0x29,0x65,0x81,0,0xc0};
static const uint8_t mouse[]={0x05,1,0x09,2,0xa1,1,0x85,7,0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,
0x95,3,0x75,1,0x81,2,0x95,1,0x75,5,0x81,1,0x05,1,0x09,0x30,0x09,0x31,0x09,0x38,
0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,0xc0};
int main(void) {
    chos_hid_report_layout_t l; chos_usb_input_state_t s={0};
    assert(chos_hid_parse_report_descriptor(keyboard,sizeof(keyboard),&l)==CHOS_HID_OK);
    uint8_t k[8]={2,0,4,5,0,0,0,0};
    assert(chos_usb_decode_report(&l,&s,k,8)); assert(keys==3);
    assert(chos_usb_decode_report(&l,&s,k,8)); assert(keys==3); /* duplicates */
    k[2]=1; assert(chos_usb_decode_report(&l,&s,k,8)); assert(releases==0); /* rollover */
    memset(k,0,8);assert(chos_usb_decode_report(&l,&s,k,8));assert(releases==3);
    unsigned before=events;assert(!chos_usb_decode_report(&l,&s,k,7));assert(events==before);
    assert(chos_hid_parse_report_descriptor(mouse,sizeof(mouse),&l)==CHOS_HID_OK);
    uint8_t m[]={7,1,0xff,2,1};
    assert(chos_usb_decode_report(&l,&s,m,sizeof(m)));assert(dx==-1 && dy==2 && scroll==-1 && buttons==1);
    m[0]=8;before=events;assert(!chos_usb_decode_report(&l,&s,m,sizeof(m)));assert(events==before);
    chos_usb_release_input(&s);assert(buttons==2);chos_usb_release_input(&s);assert(buttons==2);
    /* The same key held by two report IDs is released only after both let go. */
    memset(&l,0,sizeof(l)); memset(&s,0,sizeof(s));
    l.uses_report_ids=1;l.field_count=2;
    for(unsigned id=1;id<=2;++id) {
        l.input_bits[id]=8;
        l.fields[id-1]=(chos_hid_field_t){.report_id=(uint8_t)id,.usage_page=7,.usage=4,.bit_size=1,.count=1,.flags=CHOS_HID_FIELD_VARIABLE};
    }
    uint8_t r1[]={1,1},r2[]={2,1};unsigned old_keys=keys,old_releases=releases;
    assert(chos_usb_decode_report(&l,&s,r1,2));assert(chos_usb_decode_report(&l,&s,r2,2));assert(keys==old_keys+1);
    r1[1]=0;assert(chos_usb_decode_report(&l,&s,r1,2));assert(releases==old_releases);
    r2[1]=0;assert(chos_usb_decode_report(&l,&s,r2,2));assert(releases==old_releases+1);
    /* Absolute pointer range scaling, unsigned maxima and atomic rejection. */
    const uint8_t tablet[]={0x05,1,0x09,2,0xa1,1,0x09,0x30,0x09,0x31,0x15,0,0x26,0xff,0x7f,0x75,16,0x95,2,0x81,2,0xc0};
    assert(chos_hid_parse_report_descriptor(tablet,sizeof(tablet),&l)==CHOS_HID_OK);
    uint8_t coords[]={0,0,0xff,0x7f};
    assert(chos_usb_decode_report(&l,&s,coords,4) && abs_x==0 && abs_y==32767);
    coords[0]=0;coords[1]=0x40;coords[2]=0;coords[3]=0x40;
    assert(chos_usb_decode_report(&l,&s,coords,4) && abs_x==16384 && abs_y==16384);
    l.fields[0].logical_max=0;before=events;
    assert(!chos_usb_decode_report(&l,&s,coords,4) && events==before);
    const uint8_t unsigned_axis[]={0x05,1,0x09,2,0xa1,1,0x09,0x30,0x15,0,0x25,0xff,0x75,8,0x95,1,0x81,2,0xc0};
    assert(chos_hid_parse_report_descriptor(unsigned_axis,sizeof(unsigned_axis),&l)==CHOS_HID_OK);
    assert(l.fields[0].logical_max==255);uint8_t max=255;
    assert(chos_usb_decode_report(&l,&s,&max,1) && abs_x==32767);
    uint8_t config[]={9,2,34,0,1,1,0,0x80,50,9,4,0,0,1,3,1,1,0,9,0x21,0x11,1,0,1,0x22,45,0,7,5,0x81,3,8,0,10};
    chos_usb_hid_interface_t out[CHOS_USB_MAX_HID];size_t count;
    assert(chos_usb_parse_config(config,sizeof(config),out,&count) && count==1);
    assert(out[0].endpoint==0x81 && out[0].report_bytes==45);
    for(size_t n=0;n<sizeof(config);++n) assert(!chos_usb_parse_config(config,n,out,&count));
    config[27]=0;assert(!chos_usb_parse_config(config,sizeof(config),out,&count));config[27]=7;
    config[14]=9;assert(!chos_usb_parse_config(config,sizeof(config),out,&count));config[14]=3;
    config[32]=0xff;assert(!chos_usb_parse_config(config,sizeof(config),out,&count));
    uint8_t hub_config[]={9,2,25,0,1,1,0,0x80,50,9,4,0,0,1,9,0,0,0,7,5,0x81,3,2,0,12};
    chos_usb_hid_interface_t hub;
    assert(chos_usb_parse_hub_config(hub_config,sizeof(hub_config),&hub));
    assert(hub.endpoint==0x81 && hub.packet_bytes==2);
    for(size_t n=0;n<sizeof(hub_config);++n)assert(!chos_usb_parse_hub_config(hub_config,n,&hub));
    hub_config[18]=0;assert(!chos_usb_parse_hub_config(hub_config,sizeof(hub_config),&hub));hub_config[18]=7;
    hub_config[22]=0;assert(!chos_usb_parse_hub_config(hub_config,sizeof(hub_config),&hub));
    uint8_t ss_config[]={9,2,40,0,1,1,0,0x80,50,9,4,0,0,1,3,1,1,0,9,0x21,0x11,1,0,1,0x22,45,0,7,5,0x81,3,8,0,10,6,0x30,0,0,8,0};
    assert(chos_usb_parse_config(ss_config,sizeof(ss_config),out,&count));
    ss_config[36]=1;assert(!chos_usb_parse_config(ss_config,sizeof(ss_config),out,&count));
    chos_xhci_ring_t r;chos_xhci_ring_init(&r);
    for(unsigned wrap=0;wrap<5;++wrap) {
        unsigned cycle=r.cycle;
        for(unsigned i=0;i<CHOS_XHCI_RING_TRBS-1;++i) {
            uint64_t address=chos_xhci_ring_push(&r,0x1234,8,2u<<10);
            assert(address==(uintptr_t)&r.trbs[i]); assert((r.trbs[i].control&1)==cycle);
        }
        assert(r.enqueue==0 && r.cycle==(cycle^1));
        assert(r.trbs[CHOS_XHCI_RING_TRBS-1].parameter==(uintptr_t)r.trbs);
        assert((r.trbs[CHOS_XHCI_RING_TRBS-1].control&3)==(cycle|2));
    }
    r.enqueue=CHOS_XHCI_RING_TRBS;assert(!chos_xhci_ring_push(&r,0,0,0));
    puts("USB configuration, report IDs, rollover, signed motion, releases and xHCI ring wrap passed");
}
