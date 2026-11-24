#include "KeyBackend.h"
#include <chos/user.h>

int chos_webcore_key_entropy(void *context, unsigned char *output, size_t length)
{
    (void)context;
    size_t offset = 0;
    unsigned retries = 0;
    while (offset < length) {
        size_t request = length - offset;
        if (request > CHOS_SYS_MAX_RANDOM_BYTES)
            request = CHOS_SYS_MAX_RANDOM_BYTES;
        long count = chos_random_bytes(output + offset, request);
        if (count > 0 && (size_t)count <= request) {
            offset += (size_t)count;
            continue;
        }
        if ((count == -CHOS_ERRNO_EAGAIN || count == -CHOS_ERRNO_EINTR)
            && retries++ < 32 && !chos_sleep_ticks(1))
            continue;
        return CHOS_KEY_NO_ENTROPY;
    }
    return 0;
}
