#ifndef CHOS_POWER_H
#define CHOS_POWER_H
#include <stdint.h>
typedef enum { CHOS_POWER_SHUTDOWN=1, CHOS_POWER_RESTART=2 } chos_power_action_t;
/* Firmware-declared PSCI conduit: 0 unsupported, 1 SMC, 2 HVC. */
void chos_power_set_conduit(unsigned conduit);
/* Returns an error if flush/service fails. Successful firmware transition never returns.
 * Caller paints the status before entry; shutdown holds it for at least 5 seconds. */
const char *chos_power_execute(chos_power_action_t action);
#endif
