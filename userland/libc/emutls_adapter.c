/* Checked POSIX hooks for the unmodified compiler-rt emutls implementation. */
#include <pthread.h>
#include <stdlib.h>
int __chos_emutls_mutex_lock(pthread_mutex_t *m){int rc=pthread_mutex_lock(m);if(rc)abort();return 0;}
int __chos_emutls_mutex_unlock(pthread_mutex_t *m){int rc=pthread_mutex_unlock(m);if(rc)abort();return 0;}
int __chos_emutls_once(pthread_once_t *once,void (*initialize)(void)){if(pthread_once(once,initialize))abort();return 0;}
int __chos_emutls_setspecific(pthread_key_t key,const void *value){if(pthread_setspecific(key,value))abort();return 0;}
