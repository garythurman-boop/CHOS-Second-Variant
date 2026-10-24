/*
 * CHOS AArch64 UEFI development loader (0.30.0).
 *
 * Loads an AArch64 PIE kernel from the EFI System Partition, allocates a safe
 * contiguous physical range below the current kernel identity-map ceiling,
 * applies the kernel's R_AARCH64_RELATIVE relocations, locates the firmware
 * device-tree table, exits boot services, and transfers control to the kernel.
 * The kernel's device drivers remain platform-specific; this loader makes the
 * kernel image placement relocatable, not the current QEMU-virt hardware model.
 */
#include "chos/firmware_time.h"
#include <stddef.h>
#include <stdint.h>
#include "chos/boot_info.h"
#include "chos/pci_resources.h"

#define EFIAPI
#define EFI_SUCCESS ((uintptr_t)0)
#define EFI_ERROR_BIT ((uintptr_t)1u << (sizeof(uintptr_t) * 8u - 1u))
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | (uintptr_t)5u)
#define EFI_ALLOCATE_MAX_ADDRESS 1u
#define EFI_ALLOCATE_ADDRESS 2u
#define EFI_CONVENTIONAL_MEMORY 7u
#define EFI_LOADER_DATA 2u
#define EFI_FILE_MODE_READ 1u
#define EFI_PAGE_SIZE 4096u
#define KERNEL_MIN_PHYSICAL_ADDRESS ((uint64_t)0x01000000u)
#define KERNEL_MAX_PHYSICAL_ADDRESS ((uint64_t)0x0000fffffffff000u)
#define KERNEL_MAX_IMAGE_SIZE ((uint64_t)256u * 1024u * 1024u)
#define ELF_PT_LOAD 1u
#define ELF_PT_DYNAMIC 2u
#define ELF_ET_DYN 3u
#define ELF_R_AARCH64_RELATIVE 1027u
#define ELF_MAX_PROGRAM_HEADERS 32u
#define FDT_MAGIC 0xd00dfeedu

typedef uintptr_t EFI_STATUS;
typedef uintptr_t EFI_UINTN;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef uint64_t EFI_PHYSICAL_ADDRESS;

typedef struct { uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8]; } EFI_GUID;
typedef struct { uint64_t Signature; uint32_t Revision; uint32_t HeaderSize; uint32_t CRC32; uint32_t Reserved; } EFI_TABLE_HEADER;
typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;
typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    uint16_t Year; uint8_t Month, Day, Hour, Minute, Second, Pad1;
    uint32_t Nanosecond; int16_t TimeZone; uint8_t Daylight, Pad2;
} EFI_TIME;

typedef struct {
    uint32_t Type;
    uint32_t Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_PHYSICAL_ADDRESS VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    uint32_t LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType;
    uint32_t ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct {
    void *Reset;
    EFI_STATUS (EFIAPI *OutputString)(void *This, CHAR16 *String);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    void *ClearScreen;
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, uint64_t, uint64_t);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *, EFI_UINTN *, void *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *, EFI_GUID *, EFI_UINTN *, void *);
struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    void *Delete;
    EFI_FILE_READ Read;
    void *Write;
    void *GetPosition;
    void *SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    void *SetInfo;
    void *Flush;
    void *OpenEx;
    void *ReadEx;
    void *WriteEx;
    void *FlushEx;
};

typedef EFI_STATUS (EFIAPI *EFI_OPEN_VOLUME)(void *, EFI_FILE_PROTOCOL **);
typedef struct { uint64_t Revision; EFI_OPEN_VOLUME OpenVolume; } EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelFormat;
    struct { uint32_t RedMask, GreenMask, BlueMask, ReservedMask; } PixelInformation;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    EFI_UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    EFI_UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(uint32_t, uint32_t, EFI_UINTN, EFI_PHYSICAL_ADDRESS *);
    EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS, EFI_UINTN);
    EFI_STATUS (EFIAPI *GetMemoryMap)(EFI_UINTN *, EFI_MEMORY_DESCRIPTOR *, EFI_UINTN *, EFI_UINTN *, uint32_t *);
    EFI_STATUS (EFIAPI *AllocatePool)(uint32_t, EFI_UINTN, void **);
    EFI_STATUS (EFIAPI *FreePool)(void *);
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE, EFI_UINTN);
    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(uint32_t, EFI_GUID *, void *, EFI_UINTN *, EFI_HANDLE **);
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
};

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_STATUS (EFIAPI *GetTime)(chos_firmware_time_t *,void *);
} CHOS_EFI_RUNTIME_SERVICES;

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    EFI_UINTN NumberOfTableEntries;
    struct { EFI_GUID VendorGuid; void *VendorTable; } *ConfigurationTable;
};

static EFI_SYSTEM_TABLE *g_st;
static EFI_STATUS g_last_status;
static uintptr_t kernel_load_start;
static uintptr_t kernel_load_end;
static uintptr_t kernel_physical_base;
static uint64_t kernel_image_span;
static uint64_t g_largest_kernel_window_range;
static uint32_t g_kernel_window_candidate_count;

static const EFI_GUID GUID_LOADED_IMAGE = {0x5b1b31a1u,0x9562u,0x11d2u,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID GUID_SIMPLE_FS = {0x964e5b22u,0x6459u,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID GUID_FILE_INFO = {0x09576e92u,0x6d3fu,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID GUID_FDT_TABLE = {0xb1b621d5u,0xf19cu,0x41a5u,{0x83,0x0b,0xd9,0x15,0x2c,0x69,0xaa,0xe0}};
static const EFI_GUID GUID_ACPI_20 = {0x8868e871u,0xe4f1u,0x11d3u,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
static const EFI_GUID GUID_ACPI_10 = {0xeb9d2d30u,0x2d88u,0x11d3u,{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static const EFI_GUID GUID_GOP = {0x9042a9deu,0x23dcu,0x4a38u,{0x96,0xfbu,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

static CHAR16 msg_start[] = {'C','H','O','S',' ','U','E','F','I',' ','l','o','a','d','e','r', '\r','\n',0};
/* Keep one absolute data relocation so PE/COFF loaders can rebase the image. */
static CHAR16 *volatile relocation_anchor = msg_start;
static CHAR16 msg_open[] = {'O','p','e','n','i','n','g',' ','\\','E','F','I','\\','C','H','O','S','\\','C','H','O','S','.','E','L','F','\r','\n',0};
static CHAR16 msg_bad[] = {'E','R','R','O','R',':',' ','i','n','v','a','l','i','d',' ','E','L','F',' ','h','e','a','d','e','r',' ','o','r',' ','p','r','o','g','r','a','m',' ','t','a','b','l','e','\r','\n',0};
static CHAR16 msg_elf_kind[] = {'E','R','R','O','R',':',' ','k','e','r','n','e','l',' ','m','u','s','t',' ','b','e',' ','A','A','r','c','h','6','4',' ','P','I','E',' ','(','E','L','F','6','4','/','D','Y','N',')','\r','\n',0};
static CHAR16 msg_alloc[] = {'E','R','R','O','R',':',' ','E','F','I',' ','c','o','u','l','d',' ','n','o','t',' ','a','l','l','o','c','a','t','e',' ','c','o','n','t','i','g','u','o','u','s',' ','c','o','n','v','e','n','t','i','o','n','a','l',' ','R','A','M',' ','f','o','r',' ','k','e','r','n','e','l','\r','\n',0};
static CHAR16 msg_alloc_range[] = {'E','R','R','O','R',':',' ','E','F','I',' ','r','e','t','u','r','n','e','d',' ','k','e','r','n','e','l',' ','m','e','m','o','r','y',' ','o','u','t','s','i','d','e',' ','s','u','p','p','o','r','t','e','d',' ','p','h','y','s','i','c','a','l',' ','a','d','d','r','e','s','s',' ','r','a','n','g','e','\r','\n',0};
static CHAR16 msg_kernel_read[] = {'E','R','R','O','R',':',' ','c','o','u','l','d',' ','n','o','t',' ','r','e','a','d',' ','\\','E','F','I','\\','C','H','O','S','\\','C','H','O','S','.','E','L','F','\r','\n',0};
static CHAR16 msg_reloc[] = {'E','R','R','O','R',':',' ','u','n','s','u','p','p','o','r','t','e','d',' ','o','r',' ','i','n','v','a','l','i','d',' ','k','e','r','n','e','l',' ','r','e','l','o','c','a','t','i','o','n','\r','\n',0};
static CHAR16 msg_exit[] = {'E','R','R','O','R',':',' ','E','x','i','t','B','o','o','t','S','e','r','v','i','c','e','s',' ','f','a','i','l','e','d','\r','\n',0};

static void out(CHAR16 *text) {
    if (g_st != NULL && g_st->ConOut != NULL && g_st->ConOut->OutputString != NULL)
        (void)g_st->ConOut->OutputString(g_st->ConOut, text);
}

static uint32_t be32(const volatile uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4u) << 32); }

static void out_hex64(CHAR16 *label, uint64_t value) {
    static const CHAR16 digits[] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    CHAR16 line[21];
    unsigned i;
    line[0] = '0'; line[1] = 'x';
    for (i = 0u; i < 16u; ++i) {
        unsigned shift = (15u - i) * 4u;
        line[2u + i] = digits[(value >> shift) & 0x0fu];
    }
    line[18] = '\r'; line[19] = '\n'; line[20] = 0;
    out(label);
    out(line);
}

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} elf_program_header_t;

static int add_bias_to_value(uint64_t original, int64_t bias, uint64_t *relocated) {
    if (relocated == NULL) return 0;
    if (bias >= 0) {
        uint64_t amount = (uint64_t)bias;
        if (original > UINT64_MAX - amount) return 0;
        *relocated = original + amount;
    } else {
        uint64_t amount = (uint64_t)(-(bias + 1)) + 1u;
        if (original < amount) return 0;
        *relocated = original - amount;
    }
    return 1;
}

static int range_in_load_segments(const elf_program_header_t *headers, size_t header_count,
                                  uint64_t address, uint64_t length, int file_backed) {
    size_t i;
    uint64_t end;
    if (length == 0u || address > UINT64_MAX - length) return 0;
    end = address + length;
    for (i = 0u; i < header_count; ++i) {
        uint64_t seg_end;
        uint64_t span = file_backed ? headers[i].filesz : headers[i].memsz;
        if (headers[i].type != ELF_PT_LOAD || span == 0u ||
            headers[i].vaddr > UINT64_MAX - span) continue;
        seg_end = headers[i].vaddr + span;
        if (address >= headers[i].vaddr && end <= seg_end) return 1;
    }
    return 0;
}

static int file_range_matches_load(const elf_program_header_t *headers, size_t header_count,
                                   uint64_t address, uint64_t file_offset, uint64_t length) {
    size_t i;
    if (length == 0u || address > UINT64_MAX - length ||
        file_offset > UINT64_MAX - length) return 0;
    for (i = 0u; i < header_count; ++i) {
        const elf_program_header_t *ph = &headers[i];
        uint64_t address_delta;
        if (ph->type != ELF_PT_LOAD || address < ph->vaddr || file_offset < ph->offset ||
            ph->vaddr > UINT64_MAX - ph->filesz || ph->offset > UINT64_MAX - ph->filesz) continue;
        address_delta = address - ph->vaddr;
        if (address_delta <= ph->filesz && length <= ph->filesz - address_delta &&
            file_offset == ph->offset + address_delta &&
            file_offset + length <= ph->offset + ph->filesz) return 1;
    }
    return 0;
}

static int apply_kernel_relocations(uintptr_t load_base,
                                    uint64_t image_min, uint64_t image_max,
                                    int64_t load_bias,
                                    const elf_program_header_t *headers, size_t header_count,
                                    const elf_program_header_t *dynamic_header) {
    uint64_t rela_vaddr = 0u, rela_size = 0u, rela_ent = 0u;
    uint64_t image_span = image_max - image_min;
    uint8_t *dynamic_bytes;
    uint64_t offset;
    int have_rela = 0, have_rela_size = 0, have_rela_ent = 0, saw_null = 0;

    if (dynamic_header == NULL || dynamic_header->filesz < 16u ||
        dynamic_header->filesz > dynamic_header->memsz ||
        !file_range_matches_load(headers, header_count, dynamic_header->vaddr,
                                 dynamic_header->offset, dynamic_header->filesz) ||
        dynamic_header->vaddr < image_min ||
        dynamic_header->vaddr - image_min > image_span ||
        dynamic_header->filesz > image_span - (dynamic_header->vaddr - image_min)) return 0;
    dynamic_bytes = (uint8_t *)(load_base + (uintptr_t)(dynamic_header->vaddr - image_min));
    for (offset = 0u; offset + 16u <= dynamic_header->filesz; offset += 16u) {
        uint8_t *entry = dynamic_bytes + offset;
        uint64_t tag = le64(entry);
        uint64_t value = le64(entry + 8u);
        if (tag == 0u) { saw_null = 1; break; } /* DT_NULL */
        if (tag == 7u) { if (have_rela && rela_vaddr != value) return 0; rela_vaddr = value; have_rela = 1; } /* DT_RELA */
        else if (tag == 8u) { if (have_rela_size && rela_size != value) return 0; rela_size = value; have_rela_size = 1; } /* DT_RELASZ */
        else if (tag == 9u) { if (have_rela_ent && rela_ent != value) return 0; rela_ent = value; have_rela_ent = 1; } /* DT_RELAENT */
    }
    if (!saw_null || !have_rela || !have_rela_size || !have_rela_ent ||
        rela_size == 0u || rela_ent != 24u || rela_size % rela_ent != 0u ||
        !range_in_load_segments(headers, header_count, rela_vaddr, rela_size, 1)) return 0;

    {
        uint64_t count = rela_size / rela_ent;
        uint64_t i;
        if (count > 8192u) return 0;
        for (i = 0u; i < count; ++i) {
            uint8_t *rela = (uint8_t *)(load_base + (uintptr_t)(rela_vaddr - image_min) + (uintptr_t)(i * rela_ent));
            uint64_t target_vaddr = le64(rela);
            uint64_t info = le64(rela + 8u);
            int64_t addend = (int64_t)le64(rela + 16u);
            uint64_t relocated_value;
            uint64_t target_offset;
            uint64_t symbol_index = info >> 32;
            uint32_t relocation_type = (uint32_t)info;
            uint64_t original_value;
            if (symbol_index != 0u || relocation_type != ELF_R_AARCH64_RELATIVE || addend < 0 ||
                !range_in_load_segments(headers, header_count, target_vaddr, 8u, 0) ||
                target_vaddr < image_min || target_vaddr - image_min > image_span - 8u) return 0;
            original_value = (uint64_t)addend;
            if (original_value < image_min || original_value > image_max ||
                !add_bias_to_value(original_value, load_bias, &relocated_value)) return 0;
            target_offset = target_vaddr - image_min;
            *(volatile uint64_t *)(uintptr_t)(load_base + (uintptr_t)target_offset) = relocated_value;
        }
    }
    return 1;
}

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
    size_t i;
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3) return 0;
    for (i = 0; i < 8u; ++i) if (a->Data4[i] != b->Data4[i]) return 0;
    return 1;
}

static void zero_bytes(void *dst, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = 0;
}

static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
}

static int valid_fdt(uintptr_t address) {
    volatile const uint8_t *p;
    uint32_t size;
    if (address == 0u || (address & 7u) != 0u || address > UINTPTR_MAX - 40u) return 0;
    p = (volatile const uint8_t *)address;
    if (be32(p) != FDT_MAGIC) return 0;
    size = be32(p + 4u);
    return size >= 40u && size <= 16u * 1024u * 1024u && address + (uintptr_t)size > address;
}

static int firmware_has_acpi_table(void) {
    static const EFI_GUID GUID_ACPI_20 = {0x8868e871u,0xe4f1u,0x11d3u,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
    static const EFI_GUID GUID_ACPI_10 = {0xeb9d2d30u,0x2d88u,0x11d3u,{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    EFI_UINTN i;
    if (g_st == NULL || g_st->ConfigurationTable == NULL) return 0;
    for (i = 0u; i < g_st->NumberOfTableEntries; ++i) {
        if (guid_equal(&g_st->ConfigurationTable[i].VendorGuid, &GUID_ACPI_20) ||
            guid_equal(&g_st->ConfigurationTable[i].VendorGuid, &GUID_ACPI_10)) return 1;
    }
    return 0;
}

static int address_in_efi_memory_map(uintptr_t address, size_t length) {
    EFI_UINTN size = 128u * 1024u, map_key = 0u, desc_size = 0u;
    uint32_t desc_version = 0u;
    uint8_t *buffer = NULL;
    EFI_STATUS status;
    EFI_UINTN offset;
    int found = 0;
    uint64_t wanted_end = (uint64_t)address + (uint64_t)length;
    if (wanted_end <= (uint64_t)address) return 0;
    status = g_st->BootServices->AllocatePool(EFI_LOADER_DATA, size, (void **)&buffer);
    if (status != EFI_SUCCESS || buffer == NULL) return 0;
    status = g_st->BootServices->GetMemoryMap(&size, (EFI_MEMORY_DESCRIPTOR *)buffer,
                                              &map_key, &desc_size, &desc_version);
    if (status == EFI_SUCCESS && desc_size >= 40u && desc_size <= size) {
        for (offset = 0u; offset + desc_size <= size; offset += desc_size) {
            const uint8_t *desc = buffer + offset;
            uint32_t type = le32(desc);
            uint64_t physical_start = le64(desc + 8u);
            uint64_t pages = le64(desc + 24u);
            uint64_t bytes, region_end;
            if (pages > UINT64_MAX / EFI_PAGE_SIZE) continue;
            bytes = pages * EFI_PAGE_SIZE;
            if (physical_start > UINT64_MAX - bytes) continue;
            region_end = physical_start + bytes;
            if (type != 8u && type != 11u && type != 12u &&
                (uint64_t)address >= physical_start && wanted_end <= region_end) {
                found = 1;
                break;
            }
        }
    }
    (void)g_st->BootServices->FreePool(buffer);
    return found;
}

static uintptr_t find_fdt(void) {
    EFI_UINTN i;
    if (g_st == NULL || g_st->ConfigurationTable == NULL) return 0u;
    for (i = 0u; i < g_st->NumberOfTableEntries; ++i) {
        if (guid_equal(&g_st->ConfigurationTable[i].VendorGuid, &GUID_FDT_TABLE)) {
            uintptr_t address = (uintptr_t)g_st->ConfigurationTable[i].VendorTable;
            if (valid_fdt(address)) return address;
        }
    }
    /* The raw-address fallback is only for firmware without ACPI that exposes
     * the classic QEMU virt DTB at RAM base. Never probe that address on a
     * firmware that advertises ACPI but no device-tree table (e.g. VMware). */
    if (!firmware_has_acpi_table() && address_in_efi_memory_map((uintptr_t)0x40000000u, 40u) &&
        valid_fdt((uintptr_t)0x40000000u)) return (uintptr_t)0x40000000u;
    return 0u;
}

static int read_kernel(EFI_HANDLE image, uint8_t **file_bytes, size_t *file_size) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL, *file = NULL;
    EFI_UINTN info_size = 0u;
    uint8_t *info = NULL, *buffer = NULL;
    uint64_t declared_size;
    static CHAR16 path[] = {'\\','E','F','I','\\','C','H','O','S','\\','C','H','O','S','.','E','L','F',0};
    EFI_STATUS status;

    status = g_st->BootServices->HandleProtocol(image, (EFI_GUID *)&GUID_LOADED_IMAGE, (void **)&loaded);
    if (status != EFI_SUCCESS || loaded == NULL) return 0;
    status = g_st->BootServices->HandleProtocol(loaded->DeviceHandle, (EFI_GUID *)&GUID_SIMPLE_FS, (void **)&fs);
    if (status != EFI_SUCCESS || fs == NULL) return 0;
    status = fs->OpenVolume(fs, &root);
    if (status != EFI_SUCCESS || root == NULL) return 0;
    status = root->Open(root, &file, path, EFI_FILE_MODE_READ, 0u);
    (void)root->Close(root);
    if (status != EFI_SUCCESS || file == NULL) return 0;
    status = file->GetInfo(file, (EFI_GUID *)&GUID_FILE_INFO, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < 80u || info_size > 16u * 1024u * 1024u) {
        (void)file->Close(file); return 0;
    }
    status = g_st->BootServices->AllocatePool(EFI_LOADER_DATA, info_size, (void **)&info);
    if (status != EFI_SUCCESS || info == NULL) { (void)file->Close(file); return 0; }
    status = file->GetInfo(file, (EFI_GUID *)&GUID_FILE_INFO, &info_size, info);
    if (status != EFI_SUCCESS) { (void)g_st->BootServices->FreePool(info); (void)file->Close(file); return 0; }
    declared_size = le64(info + 8u);
    (void)g_st->BootServices->FreePool(info);
    if (declared_size < 64u || declared_size > 16u * 1024u * 1024u) { (void)file->Close(file); return 0; }
    status = g_st->BootServices->AllocatePool(EFI_LOADER_DATA, (EFI_UINTN)declared_size, (void **)&buffer);
    if (status != EFI_SUCCESS || buffer == NULL) { (void)file->Close(file); return 0; }
    {
        EFI_UINTN done = 0u;
        while (done < (EFI_UINTN)declared_size) {
            EFI_UINTN chunk = (EFI_UINTN)declared_size - done;
            status = file->Read(file, &chunk, buffer + done);
            if (status != EFI_SUCCESS || chunk == 0u) {
                (void)g_st->BootServices->FreePool(buffer); (void)file->Close(file); return 0;
            }
            done += chunk;
        }
    }
    (void)file->Close(file);
    *file_bytes = buffer;
    *file_size = (size_t)declared_size;
    return 1;
}

/*
 * Search all conventional RAM that can be represented by this AArch64 kernel,
 * rather than requiring a firmware-specific 1-2 GiB window. The current UEFI
 * boot path enters an early diagnostic kernel until platform-independent PMM
 * consumes this memory map; the normal QEMU ET_EXEC address layout is unchanged.
 */
static EFI_STATUS allocate_kernel_pages(EFI_UINTN pages, uint64_t span,
                                        EFI_PHYSICAL_ADDRESS *allocation_out) {
    EFI_UINTN map_size = 0u, map_key = 0u, desc_size = 0u;
    uint32_t desc_version = 0u;
    uint8_t *map = NULL;
    EFI_STATUS status;
    unsigned attempt;

    if (allocation_out == NULL || pages == 0u || span == 0u)
        return EFI_ERROR_BIT | (uintptr_t)2u;
    status = g_st->BootServices->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL || map_size == 0u) return status;
    map_size += 32u * 4096u;
    status = g_st->BootServices->AllocatePool(EFI_LOADER_DATA, map_size, (void **)&map);
    if (status != EFI_SUCCESS || map == NULL) return status;

    for (attempt = 0u; attempt < 4u; ++attempt) {
        EFI_UINTN size = map_size;
        EFI_UINTN offset;
        status = g_st->BootServices->GetMemoryMap(&size, (EFI_MEMORY_DESCRIPTOR *)map,
                                                  &map_key, &desc_size, &desc_version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            (void)g_st->BootServices->FreePool(map);
            map = NULL;
            map_size = size + 32u * 4096u;
            status = g_st->BootServices->AllocatePool(EFI_LOADER_DATA, map_size, (void **)&map);
            if (status != EFI_SUCCESS || map == NULL) return status;
            continue;
        }
        if (status != EFI_SUCCESS || desc_size < sizeof(EFI_MEMORY_DESCRIPTOR) || desc_size > size) {
            (void)g_st->BootServices->FreePool(map);
            return status != EFI_SUCCESS ? status : (EFI_ERROR_BIT | (uintptr_t)2u);
        }
        g_largest_kernel_window_range = 0u;
        g_kernel_window_candidate_count = 0u;
        for (offset = 0u; offset + desc_size <= size; offset += desc_size) {
            const uint8_t *raw = map + offset;
            uint32_t type = le32(raw);
            uint64_t start = le64(raw + 8u);
            uint64_t descriptor_pages = le64(raw + 24u);
            uint64_t bytes, end, low, high, candidate;
            EFI_PHYSICAL_ADDRESS requested;
            if (type != EFI_CONVENTIONAL_MEMORY || descriptor_pages == 0u ||
                descriptor_pages > UINT64_MAX / EFI_PAGE_SIZE) continue;
            bytes = descriptor_pages * EFI_PAGE_SIZE;
            if (start > UINT64_MAX - bytes) continue;
            end = start + bytes;
            low = start > KERNEL_MIN_PHYSICAL_ADDRESS ? start : KERNEL_MIN_PHYSICAL_ADDRESS;
            high = end < KERNEL_MAX_PHYSICAL_ADDRESS ? end : KERNEL_MAX_PHYSICAL_ADDRESS;
            if (high <= low) continue;
            ++g_kernel_window_candidate_count;
            if (high - low > g_largest_kernel_window_range)
                g_largest_kernel_window_range = high - low;
            if (high - low < span) continue;
            /* Prefer the high end of a conventional range to avoid low firmware
             * buffers; physical descriptors are page-aligned by specification. */
            candidate = (high - span) & ~((uint64_t)EFI_PAGE_SIZE - 1u);
            if (candidate < low || candidate > KERNEL_MAX_PHYSICAL_ADDRESS - span) continue;
            requested = candidate;
            status = g_st->BootServices->AllocatePages(EFI_ALLOCATE_ADDRESS,
                                                         EFI_LOADER_DATA, pages, &requested);
            if (status == EFI_SUCCESS && requested == candidate) {
                *allocation_out = requested;
                (void)g_st->BootServices->FreePool(map);
                return EFI_SUCCESS;
            }
            if (status == EFI_SUCCESS) (void)g_st->BootServices->FreePages(requested, pages);
        }
        (void)g_st->BootServices->FreePool(map);
        return EFI_ERROR_BIT | (uintptr_t)9u; /* EFI_OUT_OF_RESOURCES */
    }
    (void)g_st->BootServices->FreePool(map);
    return status;
}

static int load_elf_kernel(const uint8_t *file, size_t length, uintptr_t *entry_out) {
    uint64_t phoff, entry, minimum = UINT64_MAX, maximum = 0u;
    uint16_t phentsize, phnum;
    size_t i;
    uintptr_t load_base;
    EFI_PHYSICAL_ADDRESS allocation;
    EFI_UINTN pages;
    uint64_t image_min, image_max, span;
    int64_t load_bias;
    elf_program_header_t headers[ELF_MAX_PROGRAM_HEADERS];
    size_t header_count;
    const elf_program_header_t *dynamic_header = NULL;
    int entry_is_executable = 0;

    if (file == NULL || entry_out == NULL || length < 64u ||
        file[0] != 0x7fu || file[1] != 'E' || file[2] != 'L' || file[3] != 'F' ||
        file[4] != 2u || file[5] != 1u || file[6] != 1u || le16(file + 52u) != 64u) {
        out(msg_bad);
        return 0;
    }
    if (le16(file + 16u) != ELF_ET_DYN || le16(file + 18u) != 183u) {
        out(msg_elf_kind);
        return 0;
    }
    entry = le64(file + 24u);
    phoff = le64(file + 32u);
    phentsize = le16(file + 54u);
    phnum = le16(file + 56u);
    if (phentsize != 56u || phnum == 0u || phnum > ELF_MAX_PROGRAM_HEADERS ||
        phoff > length || (uint64_t)phnum * phentsize > length - phoff) {
        out(msg_bad);
        return 0;
    }
    header_count = (size_t)phnum;
    for (i = 0u; i < header_count; ++i) {
        const uint8_t *raw = file + (size_t)phoff + i * phentsize;
        elf_program_header_t *ph = &headers[i];
        ph->type = le32(raw);
        ph->flags = le32(raw + 4u);
        ph->offset = le64(raw + 8u);
        ph->vaddr = le64(raw + 16u);
        ph->paddr = le64(raw + 24u);
        ph->filesz = le64(raw + 32u);
        ph->memsz = le64(raw + 40u);
        ph->align = le64(raw + 48u);
        if (ph->type == ELF_PT_DYNAMIC) {
            if (dynamic_header != NULL) { out(msg_bad); return 0; }
            dynamic_header = ph;
        }
        if (ph->type != ELF_PT_LOAD) continue;
        if (ph->memsz == 0u || ph->filesz > ph->memsz || ph->vaddr != ph->paddr ||
            ph->paddr > UINT64_MAX - ph->memsz || ph->offset > length ||
            ph->filesz > length - ph->offset ||
            (ph->align > 1u && ((ph->align & (ph->align - 1u)) != 0u ||
                                (ph->vaddr % ph->align) != (ph->offset % ph->align)))) {
            out(msg_bad);
            return 0;
        }
        if (ph->vaddr <= entry && entry < ph->vaddr + ph->memsz && (ph->flags & 1u) != 0u)
            entry_is_executable = 1;
        if (ph->vaddr < minimum) minimum = ph->vaddr;
        if (ph->vaddr + ph->memsz > maximum) maximum = ph->vaddr + ph->memsz;
    }
    if (minimum == UINT64_MAX || maximum <= minimum || !entry_is_executable ||
        dynamic_header == NULL || dynamic_header->filesz < 16u ||
        dynamic_header->filesz > dynamic_header->memsz ||
        dynamic_header->offset > length || dynamic_header->filesz > length - dynamic_header->offset ||
        !file_range_matches_load(headers, header_count, dynamic_header->vaddr,
                                 dynamic_header->offset, dynamic_header->filesz)) {
        out(msg_bad);
        return 0;
    }
    image_min = minimum & ~((uint64_t)EFI_PAGE_SIZE - 1u);
    if (maximum > UINT64_MAX - (EFI_PAGE_SIZE - 1u)) { out(msg_bad); return 0; }
    image_max = (maximum + EFI_PAGE_SIZE - 1u) & ~((uint64_t)EFI_PAGE_SIZE - 1u);
    if (image_max <= image_min) { out(msg_bad); return 0; }
    span = image_max - image_min;
    if (span > KERNEL_MAX_IMAGE_SIZE || image_max - image_min != span ||
        image_max > UINTPTR_MAX || span > UINTPTR_MAX) {
        out(msg_bad);
        return 0;
    }
    pages = (EFI_UINTN)(span / EFI_PAGE_SIZE);

    out_hex64((CHAR16[]){'K','e','r','n','e','l',' ','i','m','a','g','e',' ','s','p','a','n',':',' ',0}, span);
    if (span > KERNEL_MAX_IMAGE_SIZE || span > KERNEL_MAX_PHYSICAL_ADDRESS - KERNEL_MIN_PHYSICAL_ADDRESS) { out(msg_alloc); return 0; }
    allocation = 0u;
    g_last_status = allocate_kernel_pages(pages, span, &allocation);
    if (g_last_status != EFI_SUCCESS) {
        out(msg_alloc);
        out_hex64((CHAR16[]){'E','F','I',' ','s','t','a','t','u','s',':',' ',0}, (uint64_t)g_last_status);
        out_hex64((CHAR16[]){'C','o','n','v','e','n','t','i','o','n','a','l',' ','R','A','M',' ','r','a','n','g','e','s',':',' ',0},
                  (uint64_t)g_kernel_window_candidate_count);
        out_hex64((CHAR16[]){'L','a','r','g','e','s','t',' ','c','o','n','v','e','n','t','i','o','n','a','l',' ','r','a','n','g','e',':',' ',0},
                  g_largest_kernel_window_range);
        return 0;
    }
    if (allocation < KERNEL_MIN_PHYSICAL_ADDRESS || allocation > UINTPTR_MAX ||
        (allocation & (EFI_PAGE_SIZE - 1u)) != 0u ||
        allocation > KERNEL_MAX_PHYSICAL_ADDRESS - span ||
        span > UINTPTR_MAX - (uintptr_t)allocation) {
        (void)g_st->BootServices->FreePages(allocation, pages);
        out(msg_alloc_range);
        out_hex64((CHAR16[]){'A','l','l','o','c','a','t','i','o','n',' ','a','t',':',' ',0}, (uint64_t)allocation);
        return 0;
    }
    load_base = (uintptr_t)allocation;
    if (load_base >= image_min) {
        uint64_t delta = (uint64_t)load_base - image_min;
        if (delta > INT64_MAX) { (void)g_st->BootServices->FreePages(allocation, pages); out(msg_alloc); return 0; }
        load_bias = (int64_t)delta;
    } else {
        uint64_t delta = image_min - (uint64_t)load_base;
        if (delta > (uint64_t)INT64_MAX) { (void)g_st->BootServices->FreePages(allocation, pages); out(msg_alloc); return 0; }
        load_bias = -(int64_t)delta;
    }

    zero_bytes((void *)load_base, (size_t)span);
    for (i = 0u; i < header_count; ++i) {
        const elf_program_header_t *ph = &headers[i];
        if (ph->type == ELF_PT_LOAD && ph->filesz != 0u) {
            uintptr_t destination = load_base + (uintptr_t)(ph->vaddr - image_min);
            copy_bytes((void *)destination, file + (size_t)ph->offset, (size_t)ph->filesz);
        }
    }
    if (!apply_kernel_relocations(load_base, image_min, image_max,
                                  load_bias, headers, header_count, dynamic_header)) {
        (void)g_st->BootServices->FreePages(allocation, pages);
        out(msg_reloc);
        return 0;
    }
    kernel_load_start = load_base;
    kernel_load_end = load_base + (uintptr_t)span;
    kernel_physical_base = load_base;
    kernel_image_span = span;
    *entry_out = load_base + (uintptr_t)(entry - image_min);
    out_hex64((CHAR16[]){'K','e','r','n','e','l',' ','p','h','y','s','i','c','a','l',' ','b','a','s','e',':',' ',0},
              (uint64_t)load_base);
    out_hex64((CHAR16[]){'K','e','r','n','e','l',' ','e','n','t','r','y',':',' ',0}, (uint64_t)*entry_out);
    return 1;
}

static void clean_kernel_caches(void) {
    uint64_t ctr;
    uintptr_t line, address;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line = (uintptr_t)4u << ((ctr >> 16) & 0x0fu);
    if (line < 4u || (line & (line - 1u)) != 0u) line = 64u;
    for (address = kernel_load_start; address < kernel_load_end; address += line) {
        __asm__ volatile("dc cvau, %0" :: "r"(address) : "memory");
    }
    __asm__ volatile("dsb ish\n\tic iallu\n\tdsb sy\n\tisb" ::: "memory");
}

/* The loader disables the firmware's EL1 data cache before entering the
 * kernel. Clean the handoff structure and final memory map to PoC first so
 * dirty write-back cache lines cannot hide the latest values from the kernel. */
static void clean_data_range_to_poc(uintptr_t start, uint64_t length) {
    uint64_t ctr, end64;
    uintptr_t line, address, end;
    if (length == 0u || start > UINTPTR_MAX - length) return;
    end64 = (uint64_t)start + length;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line = (uintptr_t)4u << ((ctr >> 16) & 0x0fu);
    if (line < 4u || (line & (line - 1u)) != 0u) line = 64u;
    address = start & ~(line - 1u);
    if (end64 > UINTPTR_MAX - (line - 1u)) end = UINTPTR_MAX & ~(line - 1u);
    else end = ((uintptr_t)end64 + line - 1u) & ~(line - 1u);
    for (; address < end; address += line) {
        __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
        if (address > UINTPTR_MAX - line) break;
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static void clean_boot_info_caches(const chos_boot_info_t *info) {
    if (info == NULL) return;
    clean_data_range_to_poc((uintptr_t)info, sizeof(*info));
    clean_data_range_to_poc((uintptr_t)info->memory_map_address, info->memory_map_size);
}

__attribute__((noreturn)) static void jump_to_kernel(uintptr_t entry, uintptr_t boot_info_address) {
    __asm__ volatile(
        "dsb sy\n"
        "ic iallu\n"
        "dsb sy\n"
        "isb\n"
        "mrs x9, sctlr_el1\n"
        "mov x10, #0x1005\n" /* clear SCTLR_EL1.M, C, and I */
        "bic x9, x9, x10\n"
        "msr sctlr_el1, x9\n"
        "dsb sy\n"
        "isb\n"
        "mov x0, %0\n"
        "br %1\n"
        :: "r"((uint64_t)boot_info_address), "r"((uint64_t)entry) : "x0", "x9", "x10", "memory");
    for (;;) { __asm__ volatile("wfe"); }
}

static uintptr_t find_acpi_rsdp(void) {
    EFI_UINTN i;
    uintptr_t acpi10 = 0u;
    if (g_st == NULL || g_st->ConfigurationTable == NULL) return 0u;
    for (i = 0u; i < g_st->NumberOfTableEntries; ++i) {
        const EFI_GUID *guid = &g_st->ConfigurationTable[i].VendorGuid;
        uintptr_t address = (uintptr_t)g_st->ConfigurationTable[i].VendorTable;
        if (guid_equal(guid, &GUID_ACPI_20) && address != 0u) return address;
        if (guid_equal(guid, &GUID_ACPI_10) && address != 0u) acpi10 = address;
    }
    return acpi10;
}

typedef struct EFI_PCI_ROOT_IO EFI_PCI_ROOT_IO;
struct EFI_PCI_ROOT_IO {
    EFI_HANDLE ParentHandle;
    void *PollMem, *PollIo;
    struct { void *Read, *Write; } Mem, Io, Pci;
    void *CopyMem, *Map, *Unmap, *AllocateBuffer, *FreeBuffer, *Flush, *GetAttributes, *SetAttributes;
    EFI_STATUS (EFIAPI *Configuration)(EFI_PCI_ROOT_IO *, void **);
    uint32_t SegmentNumber;
};
static void capture_pci_windows(chos_boot_info_t *info) {
    EFI_GUID guid={0x2f707ebb,0x4a1a,0x11d4,{0x9a,0x38,0,0x90,0x27,0x3f,0xc1,0x4d}};
    EFI_HANDLE *handles=NULL;
    EFI_UINTN count=0;
    if (g_st->BootServices->LocateHandleBuffer(2u,&guid,NULL,&count,&handles)!=EFI_SUCCESS) return;
    if(count<=16) for(EFI_UINTN i=0;i<count;++i) {
        EFI_PCI_ROOT_IO *root=NULL;void *resources=NULL;
        if(g_st->BootServices->HandleProtocol(handles[i],&guid,(void **)&root)!=EFI_SUCCESS || !root || !root->Configuration) continue;
        if(root->Configuration(root,&resources)!=EFI_SUCCESS || !resources) continue;
        /* Firmware protocol owns the stream; cap traversal at 16 descriptors,
         * reading each header before trusting its exact QWORD length. */
        const uint8_t *p=resources; size_t bytes=0;
        for(unsigned j=0;j<17;++j) {
            if(p[bytes]==0x79) { (void)chos_pci_capture_resources(info,root->SegmentNumber,p,bytes+1); break; }
            if(p[bytes]!=0x8a || p[bytes+1]!=43 || p[bytes+2]!=0) break;
            bytes+=46;
        }
    }
    g_st->BootServices->FreePool(handles);
}

static void capture_gop(chos_boot_info_t *info) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status;
    if (info == NULL || g_st == NULL || g_st->BootServices == NULL ||
        g_st->BootServices->LocateProtocol == NULL) return;
    status = g_st->BootServices->LocateProtocol((EFI_GUID *)&GUID_GOP, NULL, (void **)&gop);
    if (status != EFI_SUCCESS || gop == NULL || gop->Mode == NULL || gop->Mode->Info == NULL) return;
    info->framebuffer_base = gop->Mode->FrameBufferBase;
    info->framebuffer_size = gop->Mode->FrameBufferSize;
    info->framebuffer_width = gop->Mode->Info->HorizontalResolution;
    info->framebuffer_height = gop->Mode->Info->VerticalResolution;
    info->framebuffer_stride = gop->Mode->Info->PixelsPerScanLine;
    info->framebuffer_pixel_format = gop->Mode->Info->PixelFormat;
    info->framebuffer_red_mask = gop->Mode->Info->PixelInformation.RedMask;
    info->framebuffer_green_mask = gop->Mode->Info->PixelInformation.GreenMask;
    info->framebuffer_blue_mask = gop->Mode->Info->PixelInformation.BlueMask;
    info->framebuffer_reserved_mask = gop->Mode->Info->PixelInformation.ReservedMask;
    if (info->framebuffer_base != 0u && info->framebuffer_width >= 320u &&
        info->framebuffer_width <= 16384u && info->framebuffer_height >= 200u &&
        info->framebuffer_height <= 16384u && info->framebuffer_stride >= info->framebuffer_width &&
        info->framebuffer_stride <= 32768u &&
        (info->framebuffer_pixel_format == CHOS_GOP_PIXEL_RGBX8 ||
         info->framebuffer_pixel_format == CHOS_GOP_PIXEL_BGRX8 ||
         info->framebuffer_pixel_format == CHOS_GOP_PIXEL_BITMASK) &&
        (uint64_t)info->framebuffer_stride <= UINT64_MAX / 4u / info->framebuffer_height &&
        (uint64_t)info->framebuffer_stride * info->framebuffer_height * 4u <= info->framebuffer_size)
        info->flags |= CHOS_BOOT_FLAG_FRAMEBUFFER;
}

static void capture_firmware_tables(chos_boot_info_t *info) {
    if (info == NULL) return;
    info->fdt_address = find_fdt();
    info->acpi_rsdp_address = find_acpi_rsdp();
    if (info->fdt_address != 0u) info->flags |= CHOS_BOOT_FLAG_FDT;
    if (info->acpi_rsdp_address != 0u) info->flags |= CHOS_BOOT_FLAG_ACPI;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    uint8_t *kernel_file = NULL;
    size_t kernel_size = 0u;
    uintptr_t kernel_entry = 0u;
    chos_boot_info_t *boot_info = NULL;
    void *map_buffer = NULL;
    EFI_UINTN map_size = 128u * 1024u, map_key = 0u, desc_size = 0u;
    uint32_t desc_version = 0u;
    unsigned attempt;
    EFI_STATUS status;

    g_st = system_table;
    out(relocation_anchor);
    out(msg_open);
    if (!read_kernel(image_handle, &kernel_file, &kernel_size)) {
        out(msg_kernel_read); return EFI_ERROR_BIT | (uintptr_t)1u;
    }
    if (!load_elf_kernel(kernel_file, kernel_size, &kernel_entry)) {
        (void)g_st->BootServices->FreePool(kernel_file);
        return EFI_ERROR_BIT | (uintptr_t)1u;
    }
    (void)g_st->BootServices->FreePool(kernel_file);

    status = g_st->BootServices->AllocatePool(EFI_LOADER_DATA, sizeof(*boot_info), (void **)&boot_info);
    if (status != EFI_SUCCESS || boot_info == NULL) { out(msg_exit); return status; }
    zero_bytes(boot_info, sizeof(*boot_info));
    boot_info->magic = CHOS_BOOT_INFO_MAGIC;
    boot_info->version = CHOS_BOOT_INFO_VERSION;
    boot_info->struct_size = (uint32_t)sizeof(*boot_info);
    boot_info->flags = CHOS_BOOT_FLAG_UEFI;
    boot_info->kernel_physical_base = kernel_physical_base;
    boot_info->kernel_image_size = kernel_image_span;
    boot_info->kernel_entry = kernel_entry;
    capture_firmware_tables(boot_info);
    capture_gop(boot_info);
    capture_pci_windows(boot_info);
    CHOS_EFI_RUNTIME_SERVICES *runtime=(CHOS_EFI_RUNTIME_SERVICES *)g_st->RuntimeServices;
    chos_firmware_time_t firmware_time;
    if(runtime && runtime->GetTime && runtime->GetTime(&firmware_time,NULL)==EFI_SUCCESS &&
       chos_firmware_time_epoch(&firmware_time,&boot_info->clock_unix_seconds)) {
        __asm__ volatile("isb; mrs %0,cntpct_el0":"=r"(boot_info->clock_counter));
        __asm__ volatile("mrs %0,cntfrq_el0":"=r"(boot_info->clock_frequency));
        if(boot_info->clock_frequency)boot_info->flags|=CHOS_BOOT_FLAG_CLOCK;
    }

    /* Never pass firmware input protocol pointers into the OS. CHOS takes
     * exclusive ownership of the machine after ExitBootServices; UEFI text/pointer
     * protocols are not a safe post-handoff input mechanism. */
    out((CHAR16[]){'U','E','F','I',' ','h','a','n','d','o','f','f',':',' ','B','o','o','t',' ','S','e','r','v','i','c','e','s',' ','w','i','l','l',' ','b','e',' ','e','x','i','t','e','d',';',' ','n','a','t','i','v','e',' ','O','S',' ','i','n','p','u','t',' ','d','r','i','v','e','r','s',' ','r','e','q','u','i','r','e','d','.','\r','\n',0});
    if ((boot_info->flags & CHOS_BOOT_FLAG_FDT) != 0u) {
        out_hex64((CHAR16[]){'F','i','r','m','w','a','r','e',' ','F','D','T',':',' ',0}, boot_info->fdt_address);
    } else if ((boot_info->flags & CHOS_BOOT_FLAG_ACPI) != 0u) {
        out((CHAR16[]){'A','C','P','I',' ','f','i','r','m','w','a','r','e',' ','d','e','t','e','c','t','e','d',',',' ','p','a','s','s','i','n','g',' ','b','o','o','t',' ','i','n','f','o',' ','t','o',' ','k','e','r','n','e','l','.','\r','\n',0});
    } else {
        out((CHAR16[]){'N','o',' ','F','D','T',' ','o','r',' ','A','C','P','I',' ','t','a','b','l','e',' ','f','o','u','n','d',';',' ','p','a','s','s','i','n','g',' ','b','o','o','t',' ','i','n','f','o','.','\r','\n',0});
    }

    status = g_st->BootServices->AllocatePool(EFI_LOADER_DATA, map_size, &map_buffer);
    if (status != EFI_SUCCESS || map_buffer == NULL) { out(msg_exit); return status; }
    for (attempt = 0u; attempt < 5u; ++attempt) {
        EFI_UINTN size = map_size;
        status = g_st->BootServices->GetMemoryMap(&size, (EFI_MEMORY_DESCRIPTOR *)map_buffer,
                                                  &map_key, &desc_size, &desc_version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            (void)g_st->BootServices->FreePool(map_buffer);
            map_buffer = NULL;
            map_size = size + 32u * 4096u;
            status = g_st->BootServices->AllocatePool(EFI_LOADER_DATA, map_size, &map_buffer);
            if (status != EFI_SUCCESS || map_buffer == NULL) { out(msg_exit); return status; }
            continue;
        }
        if (status != EFI_SUCCESS || desc_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
            desc_size > size || size % desc_size != 0u) {
            out(msg_exit);
            return status != EFI_SUCCESS ? status : (EFI_ERROR_BIT | (uintptr_t)2u);
        }
        /* No boot-service calls between this final map snapshot and
         * ExitBootServices; do not allocate or modify EFI state afterwards. */
        boot_info->memory_map_address = (uint64_t)(uintptr_t)map_buffer;
        boot_info->memory_map_size = size;
        boot_info->memory_descriptor_size = desc_size;
        boot_info->memory_descriptor_version = desc_version;
        boot_info->memory_descriptor_count = (uint32_t)(size / desc_size);
        /* The kernel assumes ownership. ExitBootServices must always be called;
         * firmware protocol function pointers are not valid OS input drivers. */
        status = g_st->BootServices->ExitBootServices(image_handle, map_key);
        if (status == EFI_SUCCESS) break;
    }
    if (status != EFI_SUCCESS) { out(msg_exit); return status; }
    clean_kernel_caches();
    clean_boot_info_caches(boot_info);
    jump_to_kernel(kernel_entry, (uintptr_t)boot_info);
}
