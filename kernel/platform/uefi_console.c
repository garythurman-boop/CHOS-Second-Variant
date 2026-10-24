#include <stddef.h>
#include <stdint.h>
#include "chos/boot_info.h"
#include "uefi_console.h"
#include "../gfx/font8x12.h"

#define RGB(r,g,b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define C_BG RGB(13, 20, 34)
#define C_PANEL RGB(25, 39, 61)
#define C_ACCENT RGB(59, 130, 246)
#define C_TEXT RGB(234, 240, 249)
#define C_MUTED RGB(159, 177, 201)
#define C_GREEN RGB(106, 218, 161)
#define C_AMBER RGB(255, 201, 102)
#define C_RED RGB(255, 118, 118)

static volatile uint8_t *fb;
static uint64_t fb_bytes;
static uint32_t fb_width, fb_height, fb_stride, fb_format;
static uint32_t fb_red_mask, fb_green_mask, fb_blue_mask, fb_reserved_mask;
static uint32_t cursor_x, cursor_y;
static int ready;
static uint32_t current_fg = C_TEXT;
static uint32_t current_bg = C_BG;

static uint32_t mask_value(uint8_t channel, uint32_t mask) {
    unsigned shift = 0u;
    uint32_t bits = mask;
    uint64_t maximum;
    while (bits != 0u && (bits & 1u) == 0u) { bits >>= 1u; ++shift; }
    if (bits == 0u) return 0u;
    maximum = bits;
    return (uint32_t)((((uint64_t)channel * maximum + 127u) / 255u) << shift) & mask;
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t rgb) {
    uint64_t index, offset;
    volatile uint8_t *p;
    uint8_t r, g, b;
    if (!ready || x >= fb_width || y >= fb_height) return;
    index = (uint64_t)y * fb_stride + x;
    if (index > UINT64_MAX / 4u) return;
    offset = index * 4u;
    if (offset > fb_bytes || fb_bytes - offset < 4u) return;
    p = fb + offset;
    r = (uint8_t)(rgb >> 16); g = (uint8_t)(rgb >> 8); b = (uint8_t)rgb;
    if (fb_format == CHOS_GOP_PIXEL_RGBX8) {
        p[0] = r; p[1] = g; p[2] = b; p[3] = 0xffu;
    } else if (fb_format == CHOS_GOP_PIXEL_BGRX8) {
        p[0] = b; p[1] = g; p[2] = r; p[3] = 0xffu;
    } else {
        uint32_t word = mask_value(r, fb_red_mask) | mask_value(g, fb_green_mask) |
                        mask_value(b, fb_blue_mask) | fb_reserved_mask;
        p[0] = (uint8_t)word; p[1] = (uint8_t)(word >> 8);
        p[2] = (uint8_t)(word >> 16); p[3] = (uint8_t)(word >> 24);
    }
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    uint32_t yy, xx;
    uint64_t x_end = (uint64_t)x + width, y_end = (uint64_t)y + height;
    if (x_end > fb_width) x_end = fb_width;
    if (y_end > fb_height) y_end = fb_height;
    if (x >= x_end || y >= y_end) return;
    for (yy = y; yy < (uint32_t)y_end; ++yy)
        for (xx = x; xx < (uint32_t)x_end; ++xx) put_pixel(xx, yy, color);
}

static void draw_char(uint32_t x, uint32_t y, char ch, uint32_t fg, uint32_t bg) {
    unsigned code = (unsigned char)ch, row, col;
    if (code < 32u || code > 126u) code = (unsigned)'?';
    for (row = 0; row < 12u; ++row) {
        uint8_t bits = chos_font8x12[code - 32u][row];
        for (col = 0; col < 8u; ++col)
            put_pixel(x + col, y + row, (bits & (uint8_t)(0x80u >> col)) ? fg : bg);
    }
}

static void newline(void) {
    cursor_x = 24u;
    cursor_y += 16u;
    if (cursor_y + 14u >= fb_height) {
        fill_rect(12u, 58u, fb_width > 24u ? fb_width - 24u : fb_width, fb_height > 76u ? fb_height - 76u : fb_height, C_BG);
        cursor_y = 68u;
    }
}

int uefi_console_init(const chos_boot_info_t *info) {
    uint64_t expected;
    if (info == NULL || !chos_boot_info_validate(info) ||
        (info->flags & CHOS_BOOT_FLAG_FRAMEBUFFER) == 0u) return 0;
    expected = (uint64_t)info->framebuffer_stride * info->framebuffer_height * 4u;
    if (expected > info->framebuffer_size) return 0;
    fb = (volatile uint8_t *)(uintptr_t)info->framebuffer_base;
    fb_bytes = info->framebuffer_size;
    fb_width = info->framebuffer_width;
    fb_height = info->framebuffer_height;
    fb_stride = info->framebuffer_stride;
    fb_format = info->framebuffer_pixel_format;
    fb_red_mask = info->framebuffer_red_mask;
    fb_green_mask = info->framebuffer_green_mask;
    fb_blue_mask = info->framebuffer_blue_mask;
    fb_reserved_mask = info->framebuffer_reserved_mask;
    cursor_x = 24u; cursor_y = 68u; ready = 1;
    fill_rect(0u, 0u, fb_width, fb_height, C_BG);
    fill_rect(0u, 0u, fb_width, 42u, C_PANEL);
    fill_rect(0u, 40u, fb_width, 3u, C_ACCENT);
    fill_rect(18u, 14u, 6u, 14u, C_ACCENT);
    fill_rect(22u, 18u, 6u, 10u, C_GREEN);
    {
        const char *title = "CHOS  |  PLATFORM INITIALIZATION";
        uint32_t x = 40u;
        while (*title != '\0' && x + 8u < fb_width) {
            draw_char(x, 14u, *title++, C_TEXT, C_PANEL); x += 8u;
        }
    }
    current_fg = C_TEXT; current_bg = C_BG;
    uefi_console_write("CHOS 0.30.0\n");
    uefi_console_write("Firmware handed control to the AArch64 kernel.\n\n");
    return 1;
}

void uefi_console_write(const char *text) {
    if (!ready || text == NULL) return;
    while (*text != '\0') {
        char ch = *text++;
        if (ch == '\r') continue;
        if (ch == '\n') { newline(); continue; }
        if (cursor_x + 8u >= fb_width - 12u) newline();
        draw_char(cursor_x, cursor_y, ch, current_fg, current_bg);
        cursor_x += 8u;
    }
}

void uefi_console_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char s[19]; unsigned i;
    s[0] = '0'; s[1] = 'x';
    for (i = 0; i < 16u; ++i) s[i + 2u] = digits[(value >> ((15u - i) * 4u)) & 0xfu];
    s[18] = '\0'; uefi_console_write(s);
}

void uefi_console_write_u64(uint64_t value) {
    char s[21]; unsigned n = 0u, i;
    if (value == 0u) { uefi_console_write("0"); return; }
    while (value != 0u && n < sizeof(s)) { s[n++] = (char)('0' + value % 10u); value /= 10u; }
    for (i = 0; i < n / 2u; ++i) { char t = s[i]; s[i] = s[n - i - 1u]; s[n - i - 1u] = t; }
    s[n] = '\0'; uefi_console_write(s);
}

static int sum_is_zero(const uint8_t *bytes, uint32_t size) {
    uint8_t sum = 0u; uint32_t i;
    for (i = 0u; i < size; ++i) sum = (uint8_t)(sum + bytes[i]);
    return sum == 0u;
}

static int range_in_firmware_map(const chos_boot_info_t *info, uint64_t address, uint64_t length) {
    const uint8_t *map;
    uint64_t offset, wanted_end;
    if (info == NULL || address == 0u || length == 0u || address > UINT64_MAX - length) return 0;
    wanted_end = address + length;
    map = (const uint8_t *)(uintptr_t)info->memory_map_address;
    for (offset = 0u; offset < info->memory_map_size; offset += info->memory_descriptor_size) {
        const chos_efi_memory_descriptor_t *desc =
            (const chos_efi_memory_descriptor_t *)(const void *)(map + offset);
        uint64_t bytes, region_end;
        if (desc->number_of_pages == 0u || desc->number_of_pages > UINT64_MAX / 4096u) continue;
        bytes = desc->number_of_pages * 4096u;
        if (desc->physical_start > UINT64_MAX - bytes) continue;
        region_end = desc->physical_start + bytes;
        if (address >= desc->physical_start && wanted_end <= region_end) return 1;
    }
    return 0;
}

static int valid_sdt(const chos_boot_info_t *info, const uint8_t *table,
                     uint32_t minimum, uint32_t *length_out) {
    uint32_t length;
    uint64_t address = (uint64_t)(uintptr_t)table;
    if (table == NULL || !range_in_firmware_map(info, address, 8u)) return 0;
    length = (uint32_t)table[4] | ((uint32_t)table[5] << 8) |
             ((uint32_t)table[6] << 16) | ((uint32_t)table[7] << 24);
    if (length < minimum || length > 1024u * 1024u ||
        !range_in_firmware_map(info, address, length) || !sum_is_zero(table, length)) return 0;
    if (length_out != NULL) *length_out = length;
    return 1;
}

static uint64_t little_u64(const uint8_t *p) {
    uint64_t v = 0u; unsigned i;
    for (i = 0; i < 8u; ++i) v |= (uint64_t)p[i] << (8u * i);
    return v;
}

static void report_acpi(const chos_boot_info_t *info, uint64_t address) {
    const uint8_t *rsdp = (const uint8_t *)(uintptr_t)address;
    uint8_t revision;
    uint64_t root_address;
    const uint8_t *root;
    uint32_t root_length, entries, i, found_madt = 0u, found_gtdt = 0u, found_mcfg = 0u;
    int xsdt = 0;
    if (address == 0u) { uefi_console_write("ACPI: not present\n"); return; }
    if ((address & 7u) != 0u || address > UINT64_MAX - 36u ||
        !range_in_firmware_map(info, address, 20u) || rsdp[0] != 'R' || rsdp[1] != 'S' || rsdp[2] != 'D' || rsdp[3] != ' ' ||
        rsdp[4] != 'P' || rsdp[5] != 'T' || rsdp[6] != 'R' || rsdp[7] != ' ' ||
        !sum_is_zero(rsdp, 20u)) {
        uefi_console_write("ACPI: RSDP signature/checksum invalid\n"); return;
    }
    revision = rsdp[15];
    if (revision >= 2u) {
        uint32_t len = (uint32_t)rsdp[20] | ((uint32_t)rsdp[21] << 8) |
                       ((uint32_t)rsdp[22] << 16) | ((uint32_t)rsdp[23] << 24);
        if (len < 36u || len > 4096u || address > UINT64_MAX - len ||
            !range_in_firmware_map(info, address, len) || !sum_is_zero(rsdp, len)) {
            uefi_console_write("ACPI: extended RSDP checksum/length invalid\n"); return;
        }
        root_address = little_u64(rsdp + 24u); xsdt = root_address != 0u;
    } else {
        root_address = (uint64_t)rsdp[16] | ((uint64_t)rsdp[17] << 8) |
                       ((uint64_t)rsdp[18] << 16) | ((uint64_t)rsdp[19] << 24);
    }
    uefi_console_write("ACPI RSDP: valid, rev "); uefi_console_write_u64(revision); uefi_console_write("\n");
    if (root_address == 0u) { uefi_console_write("ACPI: no root table pointer\n"); return; }
    root = (const uint8_t *)(uintptr_t)root_address;
    if (!valid_sdt(info, root, 36u, &root_length) ||
        (xsdt ? (root[0] != 'X' || root[1] != 'S' || root[2] != 'D' || root[3] != 'T') :
                (root[0] != 'R' || root[1] != 'S' || root[2] != 'D' || root[3] != 'T'))) {
        uefi_console_write("ACPI: root table invalid\n"); return;
    }
    entries = (root_length - 36u) / (xsdt ? 8u : 4u);
    if (entries > 256u) entries = 256u;
    for (i = 0u; i < entries; ++i) {
        uint64_t child_address = xsdt ? little_u64(root + 36u + i * 8u) :
            (uint64_t)((uint32_t)root[36u + i * 4u] | ((uint32_t)root[37u + i * 4u] << 8) |
                       ((uint32_t)root[38u + i * 4u] << 16) | ((uint32_t)root[39u + i * 4u] << 24));
        const uint8_t *child;
        uint32_t child_len;
        if (child_address == 0u || child_address > UINT64_MAX - 36u ||
            !range_in_firmware_map(info, child_address, 36u)) continue;
        child = (const uint8_t *)(uintptr_t)child_address;
        if (!valid_sdt(info, child, 36u, &child_len)) continue;
        (void)child_len;
        if (child[0] == 'A' && child[1] == 'P' && child[2] == 'I' && child[3] == 'C') found_madt = 1u;
        if (child[0] == 'G' && child[1] == 'T' && child[2] == 'D' && child[3] == 'T') found_gtdt = 1u;
        if (child[0] == 'M' && child[1] == 'C' && child[2] == 'F' && child[3] == 'G') found_mcfg = 1u;
    }
    uefi_console_write(xsdt ? "ACPI XSDT: valid\n" : "ACPI RSDT: valid\n");
    uefi_console_write("ACPI entries: "); uefi_console_write_u64(entries); uefi_console_write("; ARM interrupt table (APIC): ");
    uefi_console_write(found_madt ? "present\n" : "not found\n");
    uefi_console_write("Generic timer table (GTDT): "); uefi_console_write(found_gtdt ? "present\n" : "not found\n");
    uefi_console_write("PCI configuration table (MCFG): "); uefi_console_write(found_mcfg ? "present\n" : "not found\n");
}

void uefi_console_report_boot_info(const chos_boot_info_t *info) {
    chos_boot_memory_summary_t summary;
    if (!ready || info == NULL) return;
    uefi_console_write("Kernel image base: "); uefi_console_write_hex(info->kernel_physical_base);
    uefi_console_write("  span: "); uefi_console_write_u64(info->kernel_image_size / (1024u * 1024u));
    uefi_console_write(" MiB\n");
    uefi_console_write("Kernel entry: "); uefi_console_write_hex(info->kernel_entry); uefi_console_write("\n");
    uefi_console_write("EFI memory descriptors: "); uefi_console_write_u64(info->memory_descriptor_count); uefi_console_write("\n");
    if (chos_boot_info_memory_summary(info, &summary)) {
        uefi_console_write("Conventional RAM: "); uefi_console_write_u64(summary.conventional_bytes / (1024u * 1024u));
        uefi_console_write(" MiB in "); uefi_console_write_u64(summary.conventional_range_count); uefi_console_write(" ranges\n");
        uefi_console_write("Largest conventional range: ");
        uefi_console_write_u64(summary.largest_conventional_range / (1024u * 1024u)); uefi_console_write(" MiB\n");
        uefi_console_write("Highest conventional address: "); uefi_console_write_hex(summary.highest_conventional_end); uefi_console_write("\n");
    } else uefi_console_write("EFI memory-map summary unavailable\n");
    uefi_console_write("Framebuffer: ");
    if ((info->flags & CHOS_BOOT_FLAG_FRAMEBUFFER) != 0u) {
        uefi_console_write_u64(info->framebuffer_width); uefi_console_write("x");
        uefi_console_write_u64(info->framebuffer_height); uefi_console_write(" stride ");
        uefi_console_write_u64(info->framebuffer_stride); uefi_console_write(" format ");
        uefi_console_write_u64(info->framebuffer_pixel_format); uefi_console_write("\n");
    } else uefi_console_write("GOP framebuffer unavailable\n");
    uefi_console_write("Firmware FDT: ");
    if ((info->flags & CHOS_BOOT_FLAG_FDT) != 0u) { uefi_console_write_hex(info->fdt_address); uefi_console_write("\n"); }
    else uefi_console_write("not supplied\n");
    report_acpi(info, info->acpi_rsdp_address);
    uefi_console_write("\nPlatform status: firmware discovery complete; kernel initialization follows.\n");
    uefi_console_write("Device MMIO remains gated until its platform mapping is validated.\n");
}
