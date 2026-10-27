/* Separate TU: Clang may group nontrivial TLS initialization within one TU. */
extern "C" void chos_startup_postkey_destroyed(unsigned);
struct PostKeyObject {
 unsigned value=0;
 ~PostKeyObject(){chos_startup_postkey_destroyed(value);}
};
static thread_local PostKeyObject object;
extern "C" void chos_startup_set_postkey(unsigned value){object.value=value;}
