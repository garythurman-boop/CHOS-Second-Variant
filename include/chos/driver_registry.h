#ifndef CHOS_DRIVER_REGISTRY_H
#define CHOS_DRIVER_REGISTRY_H
#include "chos/pci.h"
#define CHOS_DRIVER_MAX 16u
#define CHOS_DRIVER_DEVICE_MAX 64u
#define CHOS_DRIVER_ANY 0xffffu
typedef enum {CHOS_BUS_PCI,CHOS_BUS_FDT} chos_driver_bus_t;
typedef enum {
    CHOS_DEVICE_DISCOVERED,CHOS_DEVICE_DEFERRED,CHOS_DEVICE_UNSUPPORTED,
    CHOS_DEVICE_READY,CHOS_DEVICE_FAILED,CHOS_DEVICE_STOPPED
} chos_driver_state_t;
enum {CHOS_DRIVER_TRANSPORT=0,CHOS_DRIVER_INPUT=1};
enum {CHOS_DRIVER_CAP_TRANSPORT=1,CHOS_DRIVER_CAP_KEYBOARD=2,CHOS_DRIVER_CAP_POINTER=4};
typedef struct {
    chos_driver_bus_t bus;
    union {chos_pci_device_t pci;struct {uintptr_t base;uint64_t size;const char *compatible;} fdt;} id;
} chos_driver_device_t;
typedef struct {const chos_boot_info_t *boot;const chos_acpi_info_t *acpi;void *context;} chos_driver_context_t;
struct chos_driver_ops;
typedef struct {
    chos_driver_device_t device;
    const struct chos_driver_ops *driver;
    chos_driver_state_t state;
    const char *reason;
    uintptr_t owner;
    uint32_t capabilities;
} chos_driver_binding_t;
typedef struct chos_driver_ops {
    const char *name;
    unsigned phase,priority;
    /* Pure identity matching: no registers, reset, allocation or DMA here. */
    int (*matches)(const chos_driver_device_t *);
    /* UNSUPPORTED means clean refusal before ownership. FAILED prevents another
     * driver from touching uncertain hardware. READY means transport ownership,
     * not proof that a keyboard, filesystem or network is operational. */
    chos_driver_state_t (*start)(const chos_driver_context_t *,chos_driver_binding_t *);
    void (*poll)(chos_driver_binding_t *);
    void (*stop)(chos_driver_binding_t *);
} chos_driver_ops_t;
typedef struct {
    const chos_driver_ops_t *drivers[CHOS_DRIVER_MAX];
    chos_driver_binding_t devices[CHOS_DRIVER_DEVICE_MAX];
    chos_driver_context_t context;
    unsigned driver_count,device_count,started;
} chos_driver_registry_t;
/* Registry storage must start zeroed and outlive its bound devices. It cannot be
 * reset/rebound while drivers own DMA; register/add only before the first probe. */
int chos_driver_register(chos_driver_registry_t *,const chos_driver_ops_t *);
chos_driver_binding_t *chos_driver_add(chos_driver_registry_t *,const chos_driver_device_t *);
void chos_driver_probe(chos_driver_registry_t *,unsigned phase);
void chos_driver_poll(chos_driver_registry_t *);
void chos_driver_stop(chos_driver_registry_t *);
const char *chos_driver_state_name(chos_driver_state_t);
int chos_driver_match_pci(const chos_driver_device_t *,uint16_t vendor,uint16_t product,
                          uint16_t class_code,uint16_t subclass,uint16_t interface);
int chos_driver_match_fdt(const chos_driver_device_t *,const char *compatible);
#endif
