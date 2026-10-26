/* Avoid interposing the host's C++ ABI and startup symbols. */
#define __cxa_thread_atexit_impl chos_test_thread_atexit_impl
#define __cxa_thread_atexit chos_test_thread_atexit
#define __dso_handle chos_test_dso_handle
#define __chos_libc_process_fini chos_test_process_fini
