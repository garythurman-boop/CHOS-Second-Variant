#include <unicode/uclean.h>
// A genuine executable link attempt. The complete data must be refused by the
// current4MiB image assertion. No larger linker script substitutes for it.
int main(){UErrorCode status=U_ZERO_ERROR;u_init(&status);u_cleanup();return U_FAILURE(status);}
