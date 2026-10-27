#ifndef CHOS_EXIT_REGISTRY_H
#define CHOS_EXIT_REGISTRY_H
#include <stdint.h>
#define CHOS_EXIT_CALLBACKS 128u
#define CHOS_EXIT_CALLBACK_BUDGET 512u
#define CHOS_EXIT_RECURSION 16u
#define CHOS_EXIT_TABLE_LOCK 0u
#define CHOS_EXIT_FINAL_LOCK 1u
#define CHOS_EXIT_INVALID (-1)
#define CHOS_EXIT_FULL (-2)
#define CHOS_EXIT_LOCK_ERROR (-3)
#define CHOS_EXIT_LIMIT (-4)
#define CHOS_EXIT_CLOSED (-5)
typedef struct {
 union {void (*c)(void);void (*cxx)(void *);} function;
 void *object,*dso;
 uint64_t sequence;
 unsigned active,cxx;
} chos_exit_callback_t;
/* Callers supply a normal table lock and a recursive finalization lock.
 * Registered user code runs outside the table lock; finalizers are serialized. */
typedef struct {
 chos_exit_callback_t entries[CHOS_EXIT_CALLBACKS];
 uint64_t sequence;
 unsigned closed,depth,remaining;
 int (*lock)(void *,unsigned);
 int (*unlock)(void *,unsigned);
 void *context;
} chos_exit_registry_t;
int chos_exit_register_c(chos_exit_registry_t *,void (*)(void));
int chos_exit_register_cxx(chos_exit_registry_t *,void (*)(void *),void *,void *);
int chos_exit_finalize(chos_exit_registry_t *,void *dso,int close_registry);
#endif
