#include <thread>
#include <vector>
#include <atomic>
#include <cstring>
#include "chos/user.h"
static std::vector<unsigned> values{2,3,5};
static std::atomic<unsigned> count{};
int main(int argc,char **argv){
 if(argc!=3||std::strcmp(argv[1],"alpha")||std::strcmp(argv[2],"beta")||values.size()!=3||values[2]!=5)return 1;
 std::thread workers[3];for(auto& t:workers)t=std::thread([]{count.fetch_add(1);});for(auto& t:workers)t.join();
 if(count!=3)return 2;
 const char *s="[ OK ] native SDK C++ main,constructors and threads passed\n";chos_write(s,std::strlen(s));return 23;
}
