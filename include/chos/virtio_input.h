#ifndef CHOS_VIRTIO_INPUT_H
#define CHOS_VIRTIO_INPUT_H

#include <stdint.h>

int chos_input_init(void);
void chos_input_poll(void);
unsigned chos_input_device_count(void);
const char *chos_input_last_error(void);

#endif
