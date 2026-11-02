#ifndef CHOS_RESOURCE_PROFILE_USER_H
#define CHOS_RESOURCE_PROFILE_USER_H
#include "chos/resource_profile.h"
/* Read-only CRT hint, never kernel authority. Older CRTs/CHCX fall back to
 * legacy. The kernel independently selects the validated executable profile. */
#ifdef __cplusplus
extern "C" {
#endif
extern const uint32_t __chos_resource_profile_id __attribute__((weak));
#ifdef __cplusplus
}
#endif
static inline uint32_t chos_native_resource_profile(void) {
    return &__chos_resource_profile_id && __chos_resource_profile_id==CHOS_RESOURCE_ENGINE
        ? CHOS_RESOURCE_ENGINE : CHOS_RESOURCE_LEGACY;
}
static inline uint32_t chos_native_worker_stack_bytes(void) {
    chos_execution_profile_t profile = {0};
    if(!chos_execution_profile_get(chos_native_resource_profile(), &profile))return 32768u;
    return profile.default_worker_stack_bytes;
}
#endif
