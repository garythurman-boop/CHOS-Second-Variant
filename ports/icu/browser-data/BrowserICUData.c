#include "BrowserICUData.h"
#include "DigestBackend.h"
#include <chos/user.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unicode/uclean.h>
#include <unicode/udata.h>

struct package {
    const char *path;
    size_t size;
    uint8_t sha256[32];
};

static const struct package packages[] = {
#include "browser-icu-packages.inc"
};
#define PACKAGE_COUNT (sizeof packages / sizeof packages[0])
static void *owned[PACKAGE_COUNT];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized;

static int read_package(const struct package *package, void **result)
{
    long fd = chos_open(package->path);
    if (fd < 0)
        return -1;
    chos_file_info_t info;
    const unsigned required = CHOS_FILE_INFO_SNAPSHOT | CHOS_FILE_INFO_READ_ONLY;
    int valid = !chos_file_info(fd, &info) && info.version == CHOS_FILE_INFO_VERSION
        && info.size_bytes == sizeof(info) && (info.flags & required) == required
        && info.length == package->size;
    uint8_t *data = valid ? malloc(package->size) : NULL;
    size_t offset = 0;
    while (data && offset < package->size) {
        size_t amount = package->size - offset;
        if (amount > CHOS_SYS_MAX_READ)
            amount = CHOS_SYS_MAX_READ;
        long got = chos_read(fd, data + offset, amount);
        if (got <= 0 || (size_t)got > amount)
            break;
        offset += (size_t)got;
    }
    long closed = chos_close(fd);
    if (!data || closed || offset != package->size) {
        free(data);
        return -1;
    }

    uint8_t digest[64];
    size_t length = 0;
    struct chos_digest *hash = chos_digest_create(CHOS_DIGEST_SHA256);
    valid = hash && !chos_digest_add(hash, data, offset)
        && !chos_digest_finish(hash, digest, &length) && length == sizeof(package->sha256)
        && !memcmp(digest, package->sha256, length);
    chos_digest_destroy(hash);
    if (!valid) {
        free(data);
        return -1;
    }
    *result = data;
    return 0;
}

int chos_browser_icu_init(void)
{
    if (pthread_mutex_lock(&lock))
        return -1;
    if (initialized) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    size_t index = 0;
    for (; index < PACKAGE_COUNT; ++index) {
        if (read_package(&packages[index], &owned[index]))
            break;
    }
    if (index == PACKAGE_COUNT) {
        UErrorCode status = U_ZERO_ERROR;
        for (index = 0; index < PACKAGE_COUNT; ++index) {
            udata_setCommonData(owned[index], &status);
            if (status != U_ZERO_ERROR)
                break;
        }
        if (status == U_ZERO_ERROR)
            u_init(&status);
        initialized = status == U_ZERO_ERROR;
        if (!initialized)
            u_cleanup();
    }
    if (!initialized) {
        for (index = 0; index < PACKAGE_COUNT; ++index) {
            free(owned[index]);
            owned[index] = NULL;
        }
    }
    pthread_mutex_unlock(&lock);
    return initialized ? 0 : -1;
}
