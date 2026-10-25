#ifndef CHOS_NETWORK_POLICY_H
#define CHOS_NETWORK_POLICY_H
/* Only the trusted configuration service may mutate its profile or parent.
 * The filesystem rejects noncanonical paths separately; comparison is case-insensitive. */
static inline int chos_network_profile_reserved(const char *path){
 const char *directory="/CFG",*profile="/CFG/NETWORK.CFG";
 if(!path)return 1;
 unsigned i=0;
 while(directory[i]&&path[i]){char c=path[i];if(c>='a'&&c<='z')c=(char)(c-'a'+'A');if(c!=directory[i])break;++i;}
 if(!directory[i]&&!path[i])return 1;
 i=0;
 while(profile[i]&&path[i]){char c=path[i];if(c>='a'&&c<='z')c=(char)(c-'a'+'A');if(c!=profile[i])return 0;++i;}
 return !profile[i]&&(!path[i]||path[i]=='/');
}
#endif
