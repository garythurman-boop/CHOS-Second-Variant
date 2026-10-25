#ifndef CHOS_VMXNET3_H
#define CHOS_VMXNET3_H
#include "chos/pci.h"
#include "chos/net_device.h"
int chos_vmxnet3_init(const chos_boot_info_t *,const chos_acpi_info_t *,const chos_pci_device_t *);
int chos_vmxnet3_ready(void);
const char *chos_vmxnet3_error(void);
void chos_vmxnet3_get_info(chos_net_device_info_t *);
int chos_vmxnet3_send(const void *,size_t);
int chos_vmxnet3_receive(void *,size_t,size_t *);
void chos_vmxnet3_stop(void);
#endif
