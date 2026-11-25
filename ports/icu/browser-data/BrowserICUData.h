#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Install the pinned complete ICU dataset at process startup, before other
 * ICU consumers. On success its buffers remain owned for the process lifetime. */
int chos_browser_icu_init(void);
#ifdef __cplusplus
}
#endif
