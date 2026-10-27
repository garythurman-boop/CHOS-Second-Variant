#include <atomic>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include "chos/user.h"
struct PostKey {
 std::atomic<unsigned> *count;std::mutex *exit_mutex;
 ~PostKey(){
  // The notifying thread must still own the lock during TLS destruction.
  // Checking here does not depend on another thread winning a scheduling race.
  if(exit_mutex->try_lock()){
   exit_mutex->unlock();const char *s="[FAIL] exit notification unlocked before post-key TLS destruction\n";
   chos_write(s,std::strlen(s));std::abort();
  }
  ++*count;
 }
};
extern "C" void cxx_thread_postkey_touch(std::atomic<unsigned> *p,std::mutex *m){thread_local PostKey value{p,m};(void)value;}
