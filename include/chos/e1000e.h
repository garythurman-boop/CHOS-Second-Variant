#ifndef CHOS_E1000E_H
#define CHOS_E1000E_H
#include "chos/pci.h"
#include "chos/net_device.h"
int chos_e1000e_init(const chos_boot_info_t *,const chos_acpi_info_t *,const chos_pci_device_t *);
int chos_e1000e_ready(void);
const char *chos_e1000e_error(void);
void chos_e1000e_get_info(chos_net_device_info_t *);
int chos_e1000e_send(const void *,size_t);
int chos_e1000e_receive(void *,size_t,size_t *);
void chos_e1000e_stop(void);
#endif
