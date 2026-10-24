#ifndef CHOS_VIRTIO_TRANSPORT_H
#define CHOS_VIRTIO_TRANSPORT_H
#include "chos/pci.h"
/* Device drivers use the existing register vocabulary through this bus-neutral
 * accessor. A handle may identify MMIO or a PCI common-config transport; it is
 * never dereferenced by a device driver. Slots are 0x200 apart, at most 32. */
void chos_virtio_use_mmio(uintptr_t validated_base);
unsigned chos_virtio_use_pci(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,
                            const chos_pci_device_t *devices,unsigned count);
/* Registry adapter: append only during discovery, then seal before device drivers
 * initialize queues. Repeated admission of the same BDF returns its old handle. */
uintptr_t chos_virtio_pci_admit(const chos_boot_info_t *,const chos_acpi_info_t *,const chos_pci_device_t *);
void chos_virtio_seal_bindings(void);
unsigned chos_virtio_pci_count(void);
uintptr_t chos_virtio_aperture(void);
uint32_t chos_virtio_read(uintptr_t handle,uint32_t reg);
void chos_virtio_write(uintptr_t handle,uint32_t reg,uint32_t value);
const char *chos_virtio_transport_error(void);
#endif
