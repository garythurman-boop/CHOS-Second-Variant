/* Native termination semantics for the static -fno-exceptions profile.
 * This does not implement exception objects, stack unwinding or exception_ptr. */
#include <__exception/operations.h>
#include <__exception/terminate.h>
#include <cstdlib>
#include "chos/tls_runtime.h"
namespace std { // These ABI functions deliberately have no libc++ version namespace.
namespace {void default_terminate(){::abort();}terminate_handler handler=default_terminate;}
terminate_handler set_terminate(terminate_handler next) noexcept{
 return __atomic_exchange_n(&handler,next?next:default_terminate,__ATOMIC_ACQ_REL);
}
terminate_handler get_terminate() noexcept{return __atomic_load_n(&handler,__ATOMIC_ACQUIRE);}
void terminate() noexcept{
 auto *t=__chos_libc_tls();if(t->terminating)::abort();t->terminating=1;
 get_terminate()();::abort(); // A returning handler cannot return to its caller.
}
}
