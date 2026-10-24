#include "chos/usb.h"
#include "chos/input.h"
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
int chos_usb_parse_config(const uint8_t *d, size_t n,
                         chos_usb_hid_interface_t out[CHOS_USB_MAX_HID], size_t *count) {
    size_t i, end, found = 0;
    int hid = 0, have_report = 0;
    uint8_t iface = 0, boot_protocol=0;
    uint16_t report = 0;
    if (!d || !out || !count || n < 9 || d[0] != 9 || d[1] != 2 || !d[5]) return 0;
    *count = 0; end = le16(d + 2);
    if (end < 9 || end != n) return 0;
    for (i = 9; i < end; i += d[i]) {
        size_t len;
        if (end - i < 2 || (len = d[i]) < 2 || len > end - i) return 0;
        if (d[i+1] == 4) {
            if (len < 9) return 0;
            if (d[i+5] == 9) return 0; /* hubs require a separate topology driver */
            iface = d[i+2]; boot_protocol=d[i+6]==1; hid = d[i+3] == 0 && d[i+5] == 3;
            have_report = 0; report = 0;
        } else if (d[i+1] == 0x21 && hid) {
            if (len < 6 || len != (size_t)6 + 3u*d[i+5]) return 0;
            for (size_t j = 6; j < len; j += 3) if (d[i+j] == 0x22) {
                if (have_report) return 0;
                report = le16(d+i+j+1); have_report = 1;
            }
            if (!report || report > 4096) return 0;
        } else if (d[i+1] == 5 && hid) {
            uint16_t packet;
            if (len < 7) return 0;
            if ((d[i+2] & 0x80) == 0 || (d[i+3] & 3) != 3) continue;
            packet = le16(d+i+4);
            if (!have_report || !(d[i+2] & 15) || (d[i+2] & 0x70) ||
                !packet || packet > CHOS_USB_REPORT_BYTES || !d[i+6] || found == CHOS_USB_MAX_HID) return 0;
            for (size_t j=0; j<found; ++j) if (out[j].endpoint == d[i+2]) return 0;
            out[found++] = (chos_usb_hid_interface_t){iface,d[i+2],d[i+6],boot_protocol,packet,report};
        }
    }
    *count = found; return found != 0;
}
/* HID keyboard usage -> shared evdev keycode. No ASCII injection: modifiers,
 * key releases, focus and shortcuts follow exactly the VirtIO event path. */
static uint16_t keycode(unsigned u) {
    static const uint8_t keys[] = {
        0,0,0,0,30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44,
        2,3,4,5,6,7,8,9,10,11,28,1,14,15,57,12,13,26,27,43,43,39,40,41,51,52,53,58,
        59,60,61,62,63,64,65,66,67,68,87,88,99,70,119,110,102,104,111,107,109,106,105,108,103,
        69,98,55,74,78,96,79,80,81,75,76,77,71,72,73,82,83,86,127,116,117
    };
    static const uint8_t modifiers[] = {29,42,56,125,97,54,100,126};
    if (u >= 0xe0 && u <= 0xe7) return modifiers[u-0xe0];
    return u < sizeof(keys) ? keys[u] : 0;
}
static int held(const uint8_t reports[32]) {
    for(unsigned i=0;i<32;++i) if(reports[i]) return 1;
    return 0;
}
static int transition(uint8_t reports[32],unsigned id,int down) {
    int before=held(reports);
    if(down) reports[id/8]|=(uint8_t)(1u<<(id%8));
    else reports[id/8]&=(uint8_t)~(1u<<(id%8));
    int after=held(reports);
    return before==after?0:(after?1:-1);
}
void chos_usb_release_input(chos_usb_input_state_t *s) {
    if (!s) return;
    for (unsigned k=0;k<256;++k) {
        if (held(s->keys[k]) && keycode(k)) chos_input_key(keycode(k),0);
        for(unsigned i=0;i<32;++i) s->keys[k][i]=0;
    }
    for (unsigned k=0;k<8;++k) {
        if (held(s->buttons[k])) chos_input_button((uint16_t)(0x110+k),0);
        for(unsigned i=0;i<32;++i) s->buttons[k][i]=0;
    }
}
int chos_usb_decode_report(const chos_hid_report_layout_t *l, chos_usb_input_state_t *s,
                          const uint8_t *d, size_t n) {
    uint8_t id=0, keys[256]={0}, buttons[8]={0};
    int keyboard=0, pointer=0, rollover=0;
    int32_t x=0,y=0,wheel=0,absolute_x=-1,absolute_y=-1;
    if (!l || !s || !d || !n || l->field_count > CHOS_HID_MAX_FIELDS) return 0;
    if (l->uses_report_ids) { id=*d++; --n; if (!id) return 0; }
    if (!l->input_bits[id] || n != (l->input_bits[id]+7u)/8u) return 0;
    for (size_t i=0;i<l->field_count;++i) {
        const chos_hid_field_t *f=&l->fields[i];
        if (f->report_id!=id || (f->flags & CHOS_HID_FIELD_CONSTANT)) continue;
        for (unsigned j=0;j<f->count;++j) {
            uint32_t v;
            if (!chos_hid_extract_bits(d,n,f->bit_offset+j*f->bit_size,f->bit_size,&v)) return 0;
            if (f->usage_page==7) {
                unsigned k=(f->flags & CHOS_HID_FIELD_VARIABLE)? f->usage+j : v;
                keyboard=1;
                if (!(f->flags & CHOS_HID_FIELD_VARIABLE) && k>3 &&
                    (k<f->usage_min || k>f->usage_max)) return 0;
                if (!(f->flags & CHOS_HID_FIELD_VARIABLE) && k>=1 && k<=3) rollover=1;
                if (k<256 && k>3 && v) keys[k]=1;
            } else if (f->usage_page==9) {
                pointer=1;
                if (f->usage>=1 && f->usage<=8 && v) buttons[f->usage-1]=1;
            } else if (f->usage_page==1) {
                int64_t value=(f->flags & CHOS_HID_FIELD_SIGNED)?(int64_t)chos_hid_sign_extend(v,f->bit_size):(int64_t)v;
                if(f->flags & CHOS_HID_FIELD_RELATIVE) {
                    if(value>32767)value=32767;
                    if(value< -32767)value=-32767;
                    if(f->usage==0x30)x=(int32_t)value;
                    if(f->usage==0x31)y=(int32_t)value;
                    if(f->usage==0x38)wheel=-(int32_t)value;
                } else if(f->usage==0x30 || f->usage==0x31) {
                    if(f->logical_max<=f->logical_min || value<f->logical_min || value>f->logical_max) return 0;
                    int32_t normalized=(int32_t)(((value-f->logical_min)*32767)/(f->logical_max-f->logical_min));
                    if(f->usage==0x30)absolute_x=normalized;else absolute_y=normalized;
                }
            }
        }
    }
    if (keyboard && !rollover) for (unsigned k=4;k<256;++k) {
        int change=transition(s->keys[k],id,keys[k]);
        if(change && keycode(k)) chos_input_key(keycode(k),change>0);
    }
    if (pointer) for (unsigned k=0;k<8;++k) {
        int change=transition(s->buttons[k],id,buttons[k]);
        if(change) chos_input_button((uint16_t)(0x110+k),change>0);
    }
    if(absolute_x>=0 || absolute_y>=0) chos_input_absolute(absolute_x,absolute_y);
    if (x || y) chos_input_relative(x,y);
    if (wheel) chos_input_scroll(wheel);
    return 1;
}
